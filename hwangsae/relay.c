/**
 *  Copyright 2019-2020 SK Telecom Co., Ltd.
 *    Author: Jakub Adam <jakub.adam@collabora.com>
 *            Jeongseok Kim <jeongseok.kim@sk.com>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include "relay.h"
#include "common.h"
#include "enumtypes.h"

#include <gaeguli/gaeguli.h>

#include <netinet/in.h>
#include <srt/srt.h>
#include <gio/gio.h>

const guint32 SRT_BACKLOG_LEN = 100;
const gint MAX_EPOLL_SRT_SOCKETS = 4000;
const int64_t MAX_EPOLL_WAIT_TIMEOUT_MS = 100;
const gint SRT_POLL_EVENTS = SRT_EPOLL_IN | SRT_EPOLL_ERR;

typedef struct
{
  SRTSOCKET socket;
  gchar *username;
  GSList *sources;
} SinkConnection;

typedef struct
{
  SRTSOCKET socket;
  gchar *username;
} SourceConnection;

static gchar *_make_stream_id (const gchar * username, const gchar * resource);

static void
_source_connection_free (SourceConnection * source)
{
  g_debug ("Closing source connection %d", source->socket);
  srt_close (source->socket);
  g_clear_pointer (&source->username, g_free);
  g_free (source);
}

static void
_sink_connection_remove_source (SinkConnection * sink,
    SourceConnection * source)
{
  sink->sources = g_slist_remove (sink->sources, source);
  _source_connection_free (source);
}

static void
_sink_connection_free (SinkConnection * sink)
{
  g_clear_slist (&sink->sources, (GDestroyNotify) _source_connection_free);

  g_debug ("Closing sink connection %d", sink->socket);
  srt_close (sink->socket);
  g_clear_pointer (&sink->username, g_free);
  g_free (sink);
}

struct _HwangsaeRelay
{
  GObject parent;

  GMutex lock;

  guint sink_port;
  guint source_port;
  gchar *external_ip;

  gchar *sink_uri;
  gchar *source_uri;

  SRTSOCKET sink_listen_sock;
  SRTSOCKET source_listen_sock;

  gboolean authentication;

  GInetSocketAddress *master_address;
  gchar *master_username;

  GHashTable *srtsocket_sink_map;
  GHashTable *username_sink_map;
  int poll_id;

  GThread *relay_thread;
  gboolean run_relay_thread;

  gint sink_latency;
  gint src_latency;
};

static guint hwangsae_relay_init_refcnt = 0;

/* *INDENT-OFF* */
G_DEFINE_TYPE (HwangsaeRelay, hwangsae_relay, G_TYPE_OBJECT);
/* *INDENT-ON* */

#define LOCK_RELAY \
  g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&self->lock)

enum
{
  PROP_SINK_PORT = 1,
  PROP_SOURCE_PORT,
  PROP_EXTERNAL_IP,
  PROP_AUTHENTICATION,
  PROP_MASTER_URI,
  PROP_MASTER_USERNAME,
  PROP_LAST
};

enum
{
  SIG_CALLER_ACCEPTED,
  SIG_CALLER_REJECTED,
  SIG_IO_ERROR,
  SIG_AUTHENTICATE,
  SIG_ON_PASSPHRASE_ASKED,
  SIG_ON_PBKEYLEN_ASKED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct
{
  const gchar *name;
  gint param;
  gint val;
} SrtParam;

static SrtParam srt_params[] = {
  {"SRTO_LINGER", SRTO_LINGER, 0},
  {"SRTO_TSBPMODE", SRTO_TSBPDMODE, 1}, /* Timestamp-based Packet Delivery */
  {"SRTO_RENDEZVOUS", SRTO_RENDEZVOUS, 0},      /* 0: not for rendezvous */
  {"SRTO_SNDBUFLEN", SRTO_SNDBUF, 2 * 0xb80000},
  {NULL, -1, -1},
};

static void
hwangsae_relay_remove_sink (HwangsaeRelay * self, SinkConnection * sink)
{
  if (sink->username) {
    g_hash_table_remove (self->username_sink_map, sink->username);
  }
  g_hash_table_remove (self->srtsocket_sink_map, &sink->socket);
}

static void
hwangsae_relay_finalize (GObject * object)
{
  HwangsaeRelay *self = HWANGSAE_RELAY (object);

  self->run_relay_thread = FALSE;
  g_clear_pointer (&self->relay_thread, g_thread_join);

  g_mutex_clear (&self->lock);

  g_clear_pointer (&self->sink_uri, g_free);
  g_clear_pointer (&self->source_uri, g_free);

  srt_close (self->sink_listen_sock);
  srt_close (self->source_listen_sock);

  g_clear_object (&self->master_address);
  g_clear_pointer (&self->master_username, g_free);

  g_hash_table_destroy (self->srtsocket_sink_map);
  g_hash_table_destroy (self->username_sink_map);

  g_clear_handle_id (&self->poll_id, srt_epoll_release);

  if (g_atomic_int_dec_and_test (&hwangsae_relay_init_refcnt)) {
    g_debug ("Cleaning up SRT");
    srt_cleanup ();
  }

  G_OBJECT_CLASS (hwangsae_relay_parent_class)->finalize (object);
}

static void
hwangsae_relay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  HwangsaeRelay *self = HWANGSAE_RELAY (object);

  switch (prop_id) {
    case PROP_SINK_PORT:
      self->sink_port = g_value_get_uint (value);
      break;
    case PROP_SOURCE_PORT:
      self->source_port = g_value_get_uint (value);
      break;
    case PROP_EXTERNAL_IP:
    {
      const gchar *ip = g_value_get_string (value);
      g_clear_pointer (&self->sink_uri, g_free);
      g_clear_pointer (&self->source_uri, g_free);
      g_clear_pointer (&self->external_ip, g_free);
      if (ip && ip[0] != '\0') {
        self->external_ip = g_strdup (ip);
      }
      break;
    }
    case PROP_AUTHENTICATION:
      self->authentication = g_value_get_boolean (value);
      break;
    case PROP_MASTER_URI:{
      g_autofree gchar *host = NULL;
      guint port = 0;

      if (hwangsae_common_parse_srt_uri (g_value_get_string (value), &host,
              &port)) {
        g_autoptr (GInetAddress) addr = g_inet_address_new_from_string (host);
        g_clear_object (&self->master_address);
        self->master_address =
            G_INET_SOCKET_ADDRESS (g_inet_socket_address_new (addr, port));
      }
      break;
    }
    case PROP_MASTER_USERNAME:
      g_clear_pointer (&self->master_username, g_free);
      self->master_username = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
hwangsae_relay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  HwangsaeRelay *self = HWANGSAE_RELAY (object);
  switch (prop_id) {
    case PROP_SINK_PORT:
      g_value_set_uint (value, self->sink_port);
      break;
    case PROP_SOURCE_PORT:
      g_value_set_uint (value, self->source_port);
      break;
    case PROP_EXTERNAL_IP:
      g_value_set_string (value, self->external_ip);
      break;
    case PROP_AUTHENTICATION:
      g_value_set_boolean (value, self->authentication);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
_apply_socket_options (SRTSOCKET sock)
{
  SrtParam *params = srt_params;

  for (; params->name != NULL; params++) {
    if (srt_setsockflag (sock, params->param, &params->val, sizeof (gint))) {
      g_error ("%s", srt_getlasterror_str ());
    }
  }
}

static void
_make_socket_nonblocking (SRTSOCKET sock)
{
  gint val = 0;

  if (srt_setsockflag (sock, SRTO_SNDSYN, &val, sizeof (gint))) {
    g_error ("%s", srt_getlasterror_str ());
  }
  if (srt_setsockflag (sock, SRTO_RCVSYN, &val, sizeof (gint))) {
    g_error ("%s", srt_getlasterror_str ());
  }
}

static SRTSOCKET
_srt_open_listen_sock (guint port)
{
  g_autoptr (GSocketAddress) sockaddr = NULL;
  g_autoptr (GError) error = NULL;

  SRTSOCKET listen_sock;
  gsize sockaddr_len;
  gpointer sa;

  g_debug ("Opening SRT listener (port: %" G_GUINT32_FORMAT ")", port);

  /* FIXME: use user-defined bind address */
  sockaddr = g_inet_socket_address_new_from_string ("0.0.0.0", port);
  sockaddr_len = g_socket_address_get_native_size (sockaddr);

  sa = g_alloca (sockaddr_len);

  if (!g_socket_address_to_native (sockaddr, sa, sockaddr_len, &error)) {
    goto failed;
  }

  listen_sock = srt_socket (AF_INET, SOCK_DGRAM, 0);
  _apply_socket_options (listen_sock);
  _make_socket_nonblocking (listen_sock);

  if (srt_bind (listen_sock, sa, sockaddr_len) == SRT_ERROR) {
    goto srt_failed;
  }

  if (srt_listen (listen_sock, SRT_BACKLOG_LEN) == SRT_ERROR) {
    goto srt_failed;
  }

  return listen_sock;

srt_failed:
  g_error ("%s", srt_getlasterror_str ());

  if (listen_sock != SRT_INVALID_SOCK) {
    srt_close (listen_sock);
  }

failed:
  if (error != NULL) {
    g_error ("%s", error->message);
  }

  return SRT_INVALID_SOCK;
}

static SRTSOCKET
hwangsae_relay_open_master_sock (HwangsaeRelay * self, const gchar * resource)
{
  SRTSOCKET master_sock;
  gpointer sa;
  gsize sa_len;
  GSocketAddress *addr = G_SOCKET_ADDRESS (self->master_address);
  g_autoptr (GError) error = NULL;
  g_autofree gchar *streamid = NULL;

  sa_len = g_socket_address_get_native_size (addr);
  sa = g_alloca (sa_len);

  if (!g_socket_address_to_native (addr, sa, sa_len, &error)) {
    goto failed;
  }

  master_sock = srt_socket (AF_INET, SOCK_DGRAM, 0);
  _apply_socket_options (master_sock);

  streamid = _make_stream_id (self->master_username, resource);
  srt_setsockflag (master_sock, SRTO_STREAMID, streamid, strlen (streamid));

  if (srt_connect (master_sock, sa, sa_len) == SRT_ERROR) {
    goto srt_failed;
  }

  _make_socket_nonblocking (master_sock);

  return master_sock;

srt_failed:
  g_debug ("%s", srt_getlasterror_str ());

  if (master_sock != SRT_INVALID_SOCK) {
    srt_close (master_sock);
  }

failed:
  if (error != NULL) {
    g_debug ("%s", error->message);
  }

  return SRT_INVALID_SOCK;
}

gboolean
hwangsae_relay_default_authenticate (HwangsaeRelay * self,
    HwangsaeCallerDirection direction, const GSocketAddress * addr,
    const gchar * username, const gchar * resource)
{
  /* Accept all connections. */
  return TRUE;
}

gboolean
_authentication_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer data)
{
  gboolean ret = g_value_get_boolean (handler_return);
  /* Handlers return TRUE on authentication success and we want to stop on
   * the first failure. */
  g_value_set_boolean (return_accu, ret);
  return ret;
}

static void
hwangsae_relay_class_init (HwangsaeRelayClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = hwangsae_relay_set_property;
  gobject_class->get_property = hwangsae_relay_get_property;
  gobject_class->finalize = hwangsae_relay_finalize;

  g_object_class_install_property (gobject_class, PROP_SINK_PORT,
      g_param_spec_uint ("sink-port", "SRT Binding port (from) ",
          "SRT Binding port (from)", 0, G_MAXUINT, 8888,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SOURCE_PORT,
      g_param_spec_uint ("source-port", "SRT Binding port (to) ",
          "SRT Binding port (to)", 0, G_MAXUINT, 9999,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_EXTERNAL_IP,
      g_param_spec_string ("external-ip", "Relay external IP",
          "When set, the relay will use this IP address in its source and sink "
          "URIs. Otherwise, the first available non-loopback IP is used",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AUTHENTICATION,
      g_param_spec_boolean ("authentication", "Enable authentication",
          "Enable authentication", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MASTER_URI,
      g_param_spec_string ("master-uri", "Master relay URI",
          "URI of the master relay this instance should chain into",
          NULL, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MASTER_USERNAME,
      g_param_spec_string ("master-username", "Master relay username",
          "Username this relay should use to authenticate with the master",
          NULL, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  signals[SIG_CALLER_ACCEPTED] =
      g_signal_new ("caller-accepted", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 4,
      HWANGSAE_TYPE_CALLER_DIRECTION, G_TYPE_SOCKET_ADDRESS, G_TYPE_STRING,
      G_TYPE_STRING);

  signals[SIG_CALLER_REJECTED] =
      g_signal_new ("caller-rejected", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 4,
      HWANGSAE_TYPE_CALLER_DIRECTION, G_TYPE_SOCKET_ADDRESS, G_TYPE_STRING,
      G_TYPE_STRING);

  signals[SIG_IO_ERROR] =
      g_signal_new ("io-error", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
      G_TYPE_SOCKET_ADDRESS, G_TYPE_ERROR);

  signals[SIG_AUTHENTICATE] =
      g_signal_new_class_handler ("authenticate", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_CALLBACK (hwangsae_relay_default_authenticate),
      _authentication_accumulator, NULL, NULL,
      G_TYPE_BOOLEAN, 4, HWANGSAE_TYPE_CALLER_DIRECTION, G_TYPE_SOCKET_ADDRESS,
      G_TYPE_STRING, G_TYPE_STRING);

  signals[SIG_ON_PASSPHRASE_ASKED] =
      g_signal_new ("on-passphrase-asked", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, g_signal_accumulator_first_wins, NULL, NULL,
      G_TYPE_STRING, 4, HWANGSAE_TYPE_CALLER_DIRECTION, G_TYPE_SOCKET_ADDRESS,
      G_TYPE_STRING, G_TYPE_STRING);

  signals[SIG_ON_PBKEYLEN_ASKED] =
      g_signal_new ("on-pbkeylen-asked", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, g_signal_accumulator_first_wins, NULL, NULL,
      GAEGULI_TYPE_SRT_KEY_LENGTH, 4, HWANGSAE_TYPE_CALLER_DIRECTION,
      G_TYPE_SOCKET_ADDRESS, G_TYPE_STRING, G_TYPE_STRING);
}

const gchar STREAM_ID_PREFIX[] = "#!::";

static void
_parse_stream_id (const gchar * stream_id, gchar ** username, gchar ** resource)
{
  gchar **keys;
  gchar **it;

  if (!g_str_has_prefix (stream_id, STREAM_ID_PREFIX)) {
    return;
  }

  if (username) {
    *username = NULL;
  }
  if (resource) {
    *resource = NULL;
  }

  keys = g_strsplit (stream_id + sizeof (STREAM_ID_PREFIX) - 1, ",", -1);
  for (it = keys; *it; ++it) {
    gchar **keyval;

    keyval = g_strsplit (*it, "=", 2);

    if (keyval && keyval[0] && keyval[1]) {
      if (g_str_equal (keyval[0], "u") && username) {
        g_clear_pointer (username, g_free);
        *username = g_strdup (keyval[1]);
      } else if (g_str_equal (keyval[0], "r") && resource) {
        g_clear_pointer (resource, g_free);
        *resource = g_strdup (keyval[1]);
      }
    }

    g_strfreev (keyval);
  }

  g_strfreev (keys);
}

static gchar *
_make_stream_id (const gchar * username, const gchar * resource)
{
  return g_strdup_printf ("%su=%s,r=%s", STREAM_ID_PREFIX, username, resource);
}

static GSocketAddress *
_peeraddr_to_g_socket_address (const struct sockaddr *peeraddr)
{
  gsize peeraddr_len;

  switch (peeraddr->sa_family) {
    case AF_INET:
      peeraddr_len = sizeof (struct sockaddr_in);
      break;
    case AF_INET6:
      peeraddr_len = sizeof (struct sockaddr_in6);
      break;
    default:
      g_warning ("Unsupported address family %d", peeraddr->sa_family);
      return NULL;
  }

  return g_socket_address_new_from_native ((gpointer) peeraddr, peeraddr_len);
}

static gboolean
hwangsae_relay_set_socket_encryption (HwangsaeRelay * self, SRTSOCKET sock,
    HwangsaeCallerDirection direction, const GSocketAddress * addr,
    const gchar * username, const gchar * resource)
{
  g_autofree gchar *passphrase = NULL;
  GaeguliSRTKeyLength key_length = GAEGULI_SRT_KEY_LENGTH_0;

  g_signal_emit (self, signals[SIG_ON_PASSPHRASE_ASKED], 0, direction, addr,
      username, resource, &passphrase);

  if (passphrase && srt_setsockflag (sock, SRTO_PASSPHRASE, passphrase,
          strlen (passphrase))) {
    g_warning ("Failed to set passphrase: %s", srt_getlasterror_str ());
    return FALSE;
  }

  g_signal_emit (self, signals[SIG_ON_PBKEYLEN_ASKED], 0, direction, addr,
      username, resource, &key_length);

  if (srt_setsockflag (sock, SRTO_PBKEYLEN, &key_length, sizeof (key_length))) {
    g_warning ("Failed to set pbkeylen: %s", srt_getlasterror_str ());
    return FALSE;
  }

  return TRUE;
}

static gint
hwangsae_relay_authenticate_sink (HwangsaeRelay * self, SRTSOCKET sock,
    gint hs_version, const struct sockaddr *peeraddr, const gchar * stream_id)
{
  g_autoptr (GSocketAddress) addr = _peeraddr_to_g_socket_address (peeraddr);
  g_autofree gchar *username_autofree = NULL;
  gchar *username = NULL;
  g_autofree gchar *resource = NULL;

  {
    gboolean authenticated;

    LOCK_RELAY;

    if (self->authentication) {
      _parse_stream_id (stream_id, &username, &resource);
      username_autofree = username;

      if (!username
          || g_hash_table_contains (self->username_sink_map, username)) {
        /* Sink socket must have username in its Stream ID and not been already
         * registered. */
        goto reject;
      }

      g_signal_emit (self, signals[SIG_AUTHENTICATE], 0,
          HWANGSAE_CALLER_DIRECTION_SINK, addr, username, resource,
          &authenticated);

      if (!authenticated) {
        goto reject;
      }

    } else if (g_hash_table_size (self->srtsocket_sink_map) != 0) {
      /* When authentication is off, only one sink can connect. */
      goto reject;
    }
  }

  if (!hwangsae_relay_set_socket_encryption (self, sock,
          HWANGSAE_CALLER_DIRECTION_SINK, addr, username, resource)) {
    goto reject;
  }

  return 0;

reject:
  g_signal_emit (self, signals[SIG_CALLER_REJECTED], 0,
      HWANGSAE_CALLER_DIRECTION_SINK, addr, username, resource);

  return -1;
}

static SRTSOCKET
_srt_accept (SRTSOCKET listen_socket, GSocketAddress ** peeraddr,
    gchar ** username, gchar ** resource)
{
  union
  {
    struct sockaddr_storage ss;
    struct sockaddr sa;
  } peer_sa;
  int peer_sa_len = sizeof (peer_sa);
  SRTSOCKET sock;
  gchar stream_id[512];
  gint optlen = sizeof (stream_id);

  sock = srt_accept (listen_socket, &peer_sa.sa, &peer_sa_len);

  if (srt_getsockflag (sock, SRTO_STREAMID, &stream_id, &optlen)) {
    g_warning ("Couldn't read stream ID: %s", srt_getlasterror_str ());
    return SRT_INVALID_SOCK;
  }

  _parse_stream_id (stream_id, username, resource);

  if (peeraddr) {
    *peeraddr = _peeraddr_to_g_socket_address (&peer_sa.sa);
  }

  return sock;
}

static void
hwangsae_relay_accept_sink (HwangsaeRelay * self)
{
  g_autoptr (GSocketAddress) addr = NULL;
  g_autofree gchar *username = NULL;
  g_autofree gchar *resource = NULL;
  SinkConnection *sink;
  SRTSOCKET sock;

  sock = _srt_accept (self->sink_listen_sock, &addr, &username, &resource);
  if (sock == SRT_INVALID_SOCK) {
    return;
  }

  sink = g_new0 (SinkConnection, 1);
  sink->socket = sock;
  sink->username = g_steal_pointer (&username);

  {
    g_autofree gchar *ip =
        g_inet_address_to_string (g_inet_socket_address_get_address
        (G_INET_SOCKET_ADDRESS (addr)));

    g_debug ("Accepting sink %d username: %s from %s", sock, sink->username,
        ip);
  }

  g_hash_table_insert (self->srtsocket_sink_map, &sink->socket, sink);
  if (sink->username) {
    g_hash_table_insert (self->username_sink_map, sink->username, sink);
  }

  srt_epoll_add_usock (self->poll_id, sock, &SRT_POLL_EVENTS);

  g_signal_emit (self, signals[SIG_CALLER_ACCEPTED],
      0, HWANGSAE_CALLER_DIRECTION_SINK, addr, sink->username, resource);
}

static gint
hwangsae_relay_authenticate_source (HwangsaeRelay * self, SRTSOCKET sock,
    gint hs_version, const struct sockaddr *peeraddr, const gchar * stream_id)
{
  g_autoptr (GSocketAddress) addr = _peeraddr_to_g_socket_address (peeraddr);
  g_autofree gchar *username = NULL;
  g_autofree gchar *resource = NULL;
  SinkConnection *sink = NULL;
  g_autofree gchar *ip =
      g_inet_address_to_string (g_inet_socket_address_get_address
      (G_INET_SOCKET_ADDRESS (addr)));

  {
    gboolean authenticated = TRUE;

    LOCK_RELAY;

    if (self->authentication) {
      _parse_stream_id (stream_id, &username, &resource);

      if (!resource) {
        // Source socket must specify ID of the sink stream it wants to receive.
        g_debug ("Rejecting source %d. No Resource Name found in Stream ID.",
            sock);
        goto reject;
      }

      sink = g_hash_table_lookup (self->username_sink_map, resource);
    } else if (g_hash_table_size (self->srtsocket_sink_map) != 0) {
      /* In unauthenticated mode pick the first (and likely only) sink. When
       * the relay doesn't have any connected sink, the source gets rejected. */
      GHashTableIter it;

      g_hash_table_iter_init (&it, self->srtsocket_sink_map);
      g_hash_table_iter_next (&it, NULL, (gpointer *) & sink);
    }

    if (!sink && !self->master_address) {
      /* Reject the attempt to connect an unknown sink. */
      goto reject;
    }

    if (self->authentication) {
      g_signal_emit (self, signals[SIG_AUTHENTICATE], 0,
          HWANGSAE_CALLER_DIRECTION_SRC, addr, username, resource,
          &authenticated);
    }

    if (!authenticated) {
      goto reject;
    }
  }

  if (!hwangsae_relay_set_socket_encryption (self, sock,
          HWANGSAE_CALLER_DIRECTION_SRC, addr, username, resource)) {
    goto reject;
  }

  return 0;

reject:
  g_signal_emit (self, signals[SIG_CALLER_REJECTED], 0,
      HWANGSAE_CALLER_DIRECTION_SRC, addr, username, resource);
  return -1;
}

static void
hwangsae_relay_accept_source (HwangsaeRelay * self)
{
  g_autoptr (GSocketAddress) addr = NULL;
  g_autofree gchar *username = NULL;
  g_autofree gchar *resource = NULL;
  SinkConnection *sink;
  SourceConnection *source;
  SRTSOCKET sock;
  guint sigid = SIG_CALLER_ACCEPTED;

  sock = _srt_accept (self->source_listen_sock, &addr, &username, &resource);
  if (sock == SRT_INVALID_SOCK) {
    return;
  }

  if (self->authentication) {
    sink = g_hash_table_lookup (self->username_sink_map, resource);

    if (!sink && self->master_address) {
      /* In slave mode, open sink connection to the master relay. */
      SRTSOCKET master_sock;

      master_sock = hwangsae_relay_open_master_sock (self, resource);
      if (master_sock == SRT_INVALID_SOCK) {
        g_debug ("Unable to open master SRT socket");
        goto reject;
      }

      sink = g_new0 (SinkConnection, 1);
      sink->socket = master_sock;
      sink->username = g_steal_pointer (&resource);

      g_hash_table_insert (self->srtsocket_sink_map, &sink->socket, sink);
      g_hash_table_insert (self->username_sink_map, sink->username, sink);

      srt_epoll_add_usock (self->poll_id, sink->socket, &SRT_POLL_EVENTS);
    }
  } else if (g_hash_table_size (self->srtsocket_sink_map) != 0) {
    /* In unauthenticated mode pick the first (and likely only) sink. When
     * the relay doesn't have any connected sink, the source gets rejected. */
    GHashTableIter it;

    g_hash_table_iter_init (&it, self->srtsocket_sink_map);
    g_hash_table_iter_next (&it, NULL, (gpointer *) & sink);
  }

  if (!sink) {
    goto reject;
  }

  {
    g_autofree gchar *ip =
        g_inet_address_to_string (g_inet_socket_address_get_address
        (G_INET_SOCKET_ADDRESS (addr)));

    g_debug ("Accepting source %d from %s", sock, ip);
  }

  source = g_new0 (SourceConnection, 1);
  source->socket = sock;
  source->username = g_strdup (username);

  sink->sources = g_slist_append (sink->sources, source);

  goto accept;

reject:
  srt_close (sock);
  sigid = SIG_CALLER_REJECTED;

accept:
  g_signal_emit (self, signals[sigid], 0, HWANGSAE_CALLER_DIRECTION_SRC, addr,
      username, resource);
}

static void
hwangsae_relay_emit_io_error_locked (HwangsaeRelay * self, SRTSOCKET srtsocket,
    gint code, const gchar * format, ...)
{
  g_autoptr (GSocketAddress) address = NULL;
  g_autoptr (GError) error = NULL;
  union
  {
    struct sockaddr_storage storage;
    struct sockaddr sa;
  } native;
  int sa_len;
  va_list valist;

  va_start (valist, format);
  error = g_error_new_valist (HWANGSAE_RELAY_ERROR, code, format, valist);
  va_end (valist);

  if (srt_getpeername (srtsocket, &native.sa, &sa_len) == 0) {
    address = g_socket_address_new_from_native (&native.sa, sa_len);
  } else {
    g_warning ("Couldn't read peer address.");
  }

  g_mutex_unlock (&self->lock);
  g_signal_emit (self, signals[SIG_IO_ERROR], 0, address, error);
  g_mutex_lock (&self->lock);
}

static gpointer
_relay_main (gpointer data)
{
  HwangsaeRelay *self = HWANGSAE_RELAY (data);
  SRTSOCKET readfds[MAX_EPOLL_SRT_SOCKETS];
  gchar buf[1400];

  if (self->master_address) {
    g_autofree gchar *addr_s =
        g_inet_address_to_string (g_inet_socket_address_get_address
        (self->master_address));
    g_debug ("Acting as a slave to the master relay at %s:%u", addr_s,
        g_inet_socket_address_get_port (self->master_address));
  } else {
    self->sink_listen_sock = _srt_open_listen_sock (self->sink_port);
    srt_listen_callback (self->sink_listen_sock,
        (srt_listen_callback_fn *) hwangsae_relay_authenticate_sink, self);
    srt_epoll_add_usock (self->poll_id, self->sink_listen_sock,
        &SRT_POLL_EVENTS);

    if (srt_setsockflag (self->sink_listen_sock, SRTO_LATENCY,
            &self->sink_latency, sizeof (gint))) {
      g_error ("%s", srt_getlasterror_str ());
    }

    g_debug ("URI for sink connection is %s",
        hwangsae_relay_get_sink_uri (self));
  }

  self->source_listen_sock = _srt_open_listen_sock (self->source_port);
  srt_listen_callback (self->source_listen_sock,
      (srt_listen_callback_fn *) hwangsae_relay_authenticate_source, self);
  srt_epoll_add_usock (self->poll_id, self->source_listen_sock,
      &SRT_POLL_EVENTS);

  if (srt_setsockflag (self->source_listen_sock, SRTO_LATENCY,
          &self->src_latency, sizeof (gint))) {
    g_error ("%s", srt_getlasterror_str ());
  }

  while (self->run_relay_thread) {
    gint rnum = G_N_ELEMENTS (readfds);

    if (srt_epoll_wait (self->poll_id, readfds, &rnum, 0, 0,
            MAX_EPOLL_WAIT_TIMEOUT_MS, NULL, 0, NULL, 0) > 0) {

      if (!self->run_relay_thread) {
        break;
      }

      while (rnum != 0) {
        SRTSOCKET rsocket = readfds[--rnum];

        LOCK_RELAY;

        if (rsocket == self->sink_listen_sock) {
          hwangsae_relay_accept_sink (self);
        } else if (rsocket == self->source_listen_sock) {
          hwangsae_relay_accept_source (self);
        } else {
          gint recv;
          SinkConnection *sink;

          sink = g_hash_table_lookup (self->srtsocket_sink_map, &rsocket);
          g_assert (sink != NULL);

          do {
            recv = srt_recv (rsocket, buf, sizeof (buf));

            if (recv > 0) {
              GSList *it = sink->sources;

              while (it) {
                SourceConnection *source = it->data;

                it = it->next;

                if (srt_getsockstate (source->socket) > SRTS_CONNECTED) {
                  _sink_connection_remove_source (sink, source);
                  continue;
                }

                if (srt_send (source->socket, buf, recv) < 0) {
                  hwangsae_relay_emit_io_error_locked (self, source->socket,
                      HWANGSAE_RELAY_ERROR_WRITE, "srt_send failed: %s",
                      srt_strerror (srt_getlasterror (NULL), 0));
                  _sink_connection_remove_source (sink, source);
                }
              }
            } else if (recv < 0) {
              gint error = srt_getlasterror (NULL);
              if (error == SRT_ECONNLOST) {
                hwangsae_relay_remove_sink (self, sink);
                break;
              } else if (error != SRT_EASYNCRCV) {
                hwangsae_relay_emit_io_error_locked (self, rsocket,
                    HWANGSAE_RELAY_ERROR_READ, "srt_recv failed: %s",
                    srt_strerror (error, 0));
              }
            }
          } while (recv > 0);

          if (self->master_address && sink->sources == NULL) {
            /* In slave mode, close unused sink connections. */
            hwangsae_relay_remove_sink (self, sink);
          }
        }
      }
    }
  }

  return NULL;
}

static void
hwangsae_relay_init (HwangsaeRelay * self)
{
  if (g_atomic_int_add (&hwangsae_relay_init_refcnt, 1) == 0) {
    if (srt_startup () != 0) {
      g_error ("%s", srt_getlasterror_str ());
    }
  }

  g_mutex_init (&self->lock);

  self->poll_id = srt_epoll_create ();

  self->srtsocket_sink_map = g_hash_table_new_full (g_int_hash, g_int_equal,
      NULL, (GDestroyNotify) _sink_connection_free);
  self->username_sink_map = g_hash_table_new (g_str_hash, g_str_equal);
}

void
hwangsae_relay_start (HwangsaeRelay * self)
{
  LOCK_RELAY;

  self->run_relay_thread = TRUE;
  self->relay_thread = g_thread_new ("HwangsaeRelay", _relay_main, self);
}

HwangsaeRelay *
hwangsae_relay_new (const gchar * external_ip, guint sink_port,
    guint source_port)
{
  return g_object_new (HWANGSAE_TYPE_RELAY, "external-ip", external_ip,
      "sink-port", sink_port, "source-port", source_port, NULL);
}

static gchar *
hwangsae_relay_make_uri (HwangsaeRelay * self, guint port)
{
  g_autofree gchar *local_ip = NULL;
  const gchar *ip;

  LOCK_RELAY;
  if (self->external_ip) {
    ip = self->external_ip;
  } else {
    ip = local_ip = hwangsae_common_get_local_ip ();
  }

  return g_strdup_printf ("srt://%s:%d", ip, port);
}

const gchar *
hwangsae_relay_get_sink_uri (HwangsaeRelay * self)
{
  if (!self->sink_uri) {
    self->sink_uri = hwangsae_relay_make_uri (self, self->sink_port);
  }

  return self->sink_uri;
}

const gchar *
hwangsae_relay_get_source_uri (HwangsaeRelay * self)
{
  if (!self->source_uri) {
    self->source_uri = hwangsae_relay_make_uri (self, self->source_port);
  }

  return self->source_uri;
}

void
hwangsae_relay_set_latency (HwangsaeRelay * self,
    HwangsaeCallerDirection direction, gint latency)
{
  g_return_if_fail (HWANGSAE_IS_RELAY (self));

  if (direction == HWANGSAE_CALLER_DIRECTION_SINK) {
    self->sink_latency = latency;
  } else if (direction == HWANGSAE_CALLER_DIRECTION_SRC) {
    self->src_latency = latency;
  } else {
    g_warning ("Invalid direction is requested.");
  }
}
