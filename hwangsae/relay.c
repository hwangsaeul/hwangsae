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
  HwangsaeRelay *relay;
  GSList *sources;
} SinkConnection;

typedef struct
{
  SRTSOCKET socket;
  gchar *username;
  HwangsaeRelay *relay;
} SourceConnection;

static gchar *_make_stream_id (const gchar * username, const gchar * resource);

static void
_source_connection_free (SourceConnection * source)
{
  g_debug ("Closing source connection %d", source->socket);
  g_signal_emit_by_name (source->relay, "caller-closed", source->socket);
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
  g_signal_emit_by_name (sink->relay, "caller-closed", sink->socket);
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
  SIG_CALLER_CLOSED,
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
hwangsae_relay_dispose (GObject * object)
{
  HwangsaeRelay *self = HWANGSAE_RELAY (object);

  self->run_relay_thread = FALSE;
  g_clear_pointer (&self->relay_thread, g_thread_join);

  g_mutex_clear (&self->lock);

  g_clear_pointer (&self->sink_uri, g_free);
  g_clear_pointer (&self->source_uri, g_free);

  g_clear_handle_id (&self->sink_listen_sock, srt_close);
  g_clear_handle_id (&self->source_listen_sock, srt_close);

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
_srt_open_listen_sock (guint port, gint latency)
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

  listen_sock = srt_create_socket ();

  if (srt_setsockflag (listen_sock, SRTO_LATENCY, &latency, sizeof (gint))) {
    g_error ("Failed to set SRT Latency: %s", srt_getlasterror_str ());
  }

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

  master_sock = srt_create_socket ();
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
  gobject_class->dispose = hwangsae_relay_dispose;

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
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 5,
      G_TYPE_INT, HWANGSAE_TYPE_CALLER_DIRECTION, G_TYPE_SOCKET_ADDRESS,
      G_TYPE_STRING, G_TYPE_STRING);

  signals[SIG_CALLER_REJECTED] =
      g_signal_new ("caller-rejected", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 6,
      G_TYPE_INT, HWANGSAE_TYPE_CALLER_DIRECTION, G_TYPE_SOCKET_ADDRESS,
      G_TYPE_STRING, G_TYPE_STRING, HWANGSAE_TYPE_REJECT_REASON);

  signals[SIG_CALLER_CLOSED] =
      g_signal_new ("caller-closed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);

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

static GVariantDict *
_parse_stream_id (const gchar * stream_id)
{
  GVariantDict *dict = g_variant_dict_new (NULL);
  gchar **keys;
  gchar **it;

  if (!g_str_has_prefix (stream_id, STREAM_ID_PREFIX)) {
    goto out;
  }

  keys = g_strsplit (stream_id + sizeof (STREAM_ID_PREFIX) - 1, ",", -1);
  for (it = keys; *it; ++it) {
    gchar **keyval;

    keyval = g_strsplit (*it, "=", 2);

    if (keyval && keyval[0] && keyval[1]) {
      if (g_str_equal (keyval[0], "h8l_bufsize")) {
        g_variant_dict_insert (dict, keyval[0], "i", atoi (keyval[1]));
      } else {
        g_variant_dict_insert (dict, keyval[0], "s", keyval[1]);
      }
    }

    g_strfreev (keyval);
  }

  g_strfreev (keys);

out:
  return dict;
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

static void
_apply_bufsize_suggestion (SRTSOCKET sock, HwangsaeCallerDirection direction,
    GVariantDict * parsed_id)
{
  if (parsed_id && g_variant_dict_contains (parsed_id, "h8l_bufsize")) {
    gint32 buffer = 0;
    SRT_SOCKOPT opt = (direction == HWANGSAE_CALLER_DIRECTION_SINK) ?
        SRTO_RCVBUF : SRTO_SNDBUF;

    g_variant_dict_lookup (parsed_id, "h8l_bufsize", "i", &buffer);

    if (srt_setsockflag (sock, opt, &buffer, sizeof (buffer))) {
      g_warning ("Couldn't set buffer size: %s", srt_getlasterror_str ());
    } else {
      g_debug ("Setting buffer for %d to %d B", sock, buffer);
    }
  }
}

static gint
hwangsae_relay_authenticate_sink (HwangsaeRelay * self, SRTSOCKET sock,
    gint hs_version, const struct sockaddr *peeraddr, const gchar * stream_id)
{
  g_autoptr (GSocketAddress) addr = _peeraddr_to_g_socket_address (peeraddr);
  g_autoptr (GVariantDict) parsed_id = NULL;
  const gchar *username = NULL;
  const gchar *resource = NULL;
  HwangsaeRejectReason reason;

  {
    gboolean authenticated;

    LOCK_RELAY;

    if (self->authentication) {
      parsed_id = _parse_stream_id (stream_id);
      g_variant_dict_lookup (parsed_id, "u", "&s", &username);
      g_variant_dict_lookup (parsed_id, "r", "&s", &resource);

      /* Sink socket must have username in its Stream ID and not been already
       * registered. */
      if (!username) {
        reason = HWANGSAE_REJECT_REASON_NO_USERNAME;
        goto reject;
      }
      if (g_hash_table_contains (self->username_sink_map, username)) {
        reason = HWANGSAE_REJECT_REASON_USERNAME_ALREADY_REGISTERED;
        goto reject;
      }

      g_signal_emit (self, signals[SIG_AUTHENTICATE], 0,
          HWANGSAE_CALLER_DIRECTION_SINK, addr, username, resource,
          &authenticated);

      if (!authenticated) {
        reason = HWANGSAE_REJECT_REASON_AUTHENTICATION;
        goto reject;
      }

    } else if (g_hash_table_size (self->srtsocket_sink_map) != 0) {
      /* When authentication is off, only one sink can connect. */
      reason = HWANGSAE_REJECT_REASON_TOO_MANY_SINKS;
      goto reject;
    }
  }

  if (!hwangsae_relay_set_socket_encryption (self, sock,
          HWANGSAE_CALLER_DIRECTION_SINK, addr, username, resource)) {
    reason = HWANGSAE_REJECT_REASON_ENCRYPTION;
    goto reject;
  }

  _apply_bufsize_suggestion (sock, HWANGSAE_CALLER_DIRECTION_SINK, parsed_id);

  return 0;

reject:
  g_signal_emit (self, signals[SIG_CALLER_REJECTED], 0, sock,
      HWANGSAE_CALLER_DIRECTION_SINK, addr, username, resource, reason);

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
  g_autoptr (GVariantDict) parsed_id = NULL;
  SRTSOCKET sock;
  gchar stream_id[512];
  gint optlen = sizeof (stream_id);

  sock = srt_accept (listen_socket, &peer_sa.sa, &peer_sa_len);

  if (srt_getsockflag (sock, SRTO_STREAMID, &stream_id, &optlen)) {
    g_warning ("Couldn't read stream ID: %s", srt_getlasterror_str ());
    return SRT_INVALID_SOCK;
  }

  parsed_id = _parse_stream_id (stream_id);
  g_variant_dict_lookup (parsed_id, "u", "s", username);
  g_variant_dict_lookup (parsed_id, "r", "s", resource);

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
  sink->relay = self;

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
      0, sink->socket, HWANGSAE_CALLER_DIRECTION_SINK, addr, sink->username,
      resource);
}

static gint
hwangsae_relay_authenticate_source (HwangsaeRelay * self, SRTSOCKET sock,
    gint hs_version, const struct sockaddr *peeraddr, const gchar * stream_id)
{
  g_autoptr (GSocketAddress) addr = _peeraddr_to_g_socket_address (peeraddr);
  g_autoptr (GVariantDict) parsed_id = NULL;
  const gchar *username = NULL;
  const gchar *resource = NULL;
  SinkConnection *sink = NULL;
  HwangsaeRejectReason reason;
  g_autofree gchar *ip =
      g_inet_address_to_string (g_inet_socket_address_get_address
      (G_INET_SOCKET_ADDRESS (addr)));

  {
    gboolean authenticated = TRUE;

    LOCK_RELAY;

    if (self->authentication) {
      parsed_id = _parse_stream_id (stream_id);
      g_variant_dict_lookup (parsed_id, "u", "&s", &username);
      g_variant_dict_lookup (parsed_id, "r", "&s", &resource);

      if (!resource) {
        // Source socket must specify ID of the sink stream it wants to receive.
        g_debug ("Rejecting source %d. No Resource Name found in Stream ID.",
            sock);
        reason = HWANGSAE_REJECT_REASON_NO_RESOURCE;
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
      reason = HWANGSAE_REJECT_REASON_NO_SUCH_SINK;
      goto reject;
    }

    if (self->authentication) {
      g_signal_emit (self, signals[SIG_AUTHENTICATE], 0,
          HWANGSAE_CALLER_DIRECTION_SRC, addr, username, resource,
          &authenticated);
    }

    if (!authenticated) {
      reason = HWANGSAE_REJECT_REASON_AUTHENTICATION;
      goto reject;
    }
  }

  if (!hwangsae_relay_set_socket_encryption (self, sock,
          HWANGSAE_CALLER_DIRECTION_SRC, addr, username, resource)) {
    reason = HWANGSAE_REJECT_REASON_ENCRYPTION;
    goto reject;
  }

  _apply_bufsize_suggestion (sock, HWANGSAE_CALLER_DIRECTION_SRC, parsed_id);

  return 0;

reject:
  g_signal_emit (self, signals[SIG_CALLER_REJECTED], 0, sock,
      HWANGSAE_CALLER_DIRECTION_SRC, addr, username, resource, reason);
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
  HwangsaeRejectReason reason;

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
        reason = HWANGSAE_REJECT_REASON_CANT_CONNECT_MASTER;
        goto reject;
      }

      sink = g_new0 (SinkConnection, 1);
      sink->socket = master_sock;
      sink->username = g_steal_pointer (&resource);
      sink->relay = self;

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
    reason = HWANGSAE_REJECT_REASON_NO_SUCH_SINK;
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
  source->relay = self;

  sink->sources = g_slist_append (sink->sources, source);

  g_signal_emit (self, signals[SIG_CALLER_ACCEPTED], 0, source->socket,
      HWANGSAE_CALLER_DIRECTION_SRC, addr, username, resource);

  return;

reject:
  srt_close (sock);
  g_signal_emit (self, signals[SIG_CALLER_REJECTED], 0, sock,
      HWANGSAE_CALLER_DIRECTION_SRC, addr, username, resource, reason);
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
    self->sink_listen_sock =
        _srt_open_listen_sock (self->sink_port, self->sink_latency);
    srt_listen_callback (self->sink_listen_sock,
        (srt_listen_callback_fn *) hwangsae_relay_authenticate_sink, self);
    srt_epoll_add_usock (self->poll_id, self->sink_listen_sock,
        &SRT_POLL_EVENTS);

    g_debug ("URI for sink connection is %s",
        hwangsae_relay_get_sink_uri (self));
  }

  self->source_listen_sock =
      _srt_open_listen_sock (self->source_port, self->src_latency);
  srt_listen_callback (self->source_listen_sock,
      (srt_listen_callback_fn *) hwangsae_relay_authenticate_source, self);
  srt_epoll_add_usock (self->poll_id, self->source_listen_sock,
      &SRT_POLL_EVENTS);

  while (self->run_relay_thread) {
    gint rnum = G_N_ELEMENTS (readfds);
    gint num_ready_sockets;

    num_ready_sockets = srt_epoll_wait (self->poll_id, readfds, &rnum, 0, 0,
        MAX_EPOLL_WAIT_TIMEOUT_MS, NULL, 0, NULL, 0);

    if (!self->run_relay_thread) {
      break;
    }

    if (num_ready_sockets > 0) {
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
          if (sink == NULL) {
            /* Sink has got removed meanwhile. */
            continue;
          }

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
    if (srt_startup () == -1) {
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

#define OPT_STR_MAXLEN 512

#define HWANGSAE_VARIANT_TYPE_LINGER ((const GVariantType *) "(ii)")

#define VARIANT_TYPE(vartype, optlen) { G_VARIANT_TYPE_##vartype, optlen }
#define VARIANT_TYPE_INT32   VARIANT_TYPE(INT32, sizeof (gint32))
#define VARIANT_TYPE_INT64   VARIANT_TYPE(INT64, sizeof (gint64))
#define VARIANT_TYPE_BOOLEAN VARIANT_TYPE(BOOLEAN, sizeof (gboolean))
#define VARIANT_TYPE_STRING  VARIANT_TYPE(STRING, OPT_STR_MAXLEN)

/* *INDENT-OFF* */
struct
{
  const GVariantType *type;
  gint optlen;
} srt_options[] = {
  [SRTO_MSS]                = VARIANT_TYPE_INT32,
  [SRTO_SNDSYN]             = VARIANT_TYPE_BOOLEAN,
  [SRTO_RCVSYN]             = VARIANT_TYPE_BOOLEAN,
  [SRTO_ISN]                = VARIANT_TYPE_INT32,
  [SRTO_FC]                 = VARIANT_TYPE_INT32,
  [SRTO_SNDBUF]             = VARIANT_TYPE_INT32,
  [SRTO_RCVBUF]             = VARIANT_TYPE_INT32,
  [SRTO_LINGER]             = {
      HWANGSAE_VARIANT_TYPE_LINGER,
      sizeof (struct linger)
  },
  [SRTO_UDP_SNDBUF]         = VARIANT_TYPE_INT32,
  [SRTO_UDP_RCVBUF]         = VARIANT_TYPE_INT32,
  [SRTO_RENDEZVOUS]         = VARIANT_TYPE_BOOLEAN,
  [SRTO_SNDTIMEO]           = VARIANT_TYPE_INT32,
  [SRTO_RCVTIMEO]           = VARIANT_TYPE_INT32,
  [SRTO_REUSEADDR]          = VARIANT_TYPE_BOOLEAN,
  [SRTO_MAXBW]              = VARIANT_TYPE_INT64,
  [SRTO_STATE]              = VARIANT_TYPE_INT32,
  [SRTO_EVENT]              = VARIANT_TYPE_INT32,
  [SRTO_SNDDATA]            = VARIANT_TYPE_INT32,
  [SRTO_RCVDATA]            = VARIANT_TYPE_INT32,
  [SRTO_SENDER]             = VARIANT_TYPE_BOOLEAN,
  [SRTO_TSBPDMODE]          = VARIANT_TYPE_BOOLEAN,
  [SRTO_LATENCY]            = VARIANT_TYPE_INT32,
  [SRTO_INPUTBW]            = VARIANT_TYPE_INT64,
  [SRTO_OHEADBW]            = VARIANT_TYPE_INT32,
  [SRTO_PASSPHRASE]         = VARIANT_TYPE_STRING,
  [SRTO_PBKEYLEN]           = VARIANT_TYPE_INT32,
  [SRTO_KMSTATE]            = VARIANT_TYPE_INT32,
  [SRTO_IPTTL]              = VARIANT_TYPE_INT32,
  [SRTO_IPTOS]              = VARIANT_TYPE_INT32,
  [SRTO_TLPKTDROP]          = VARIANT_TYPE_BOOLEAN,
  [SRTO_SNDDROPDELAY]       = VARIANT_TYPE_INT32,
  [SRTO_NAKREPORT]          = VARIANT_TYPE_BOOLEAN,
  [SRTO_VERSION]            = VARIANT_TYPE_INT32,
  [SRTO_PEERVERSION]        = VARIANT_TYPE_INT32,
  [SRTO_CONNTIMEO]          = VARIANT_TYPE_INT32,
  [SRTO_DRIFTTRACER]        = VARIANT_TYPE_BOOLEAN,
  [SRTO_SNDKMSTATE]         = VARIANT_TYPE_INT32,
  [SRTO_RCVKMSTATE]         = VARIANT_TYPE_INT32,
  [SRTO_LOSSMAXTTL]         = VARIANT_TYPE_INT32,
  [SRTO_RCVLATENCY]         = VARIANT_TYPE_INT32,
  [SRTO_PEERLATENCY]        = VARIANT_TYPE_INT32,
  [SRTO_MINVERSION]         = VARIANT_TYPE_INT32,
  [SRTO_STREAMID]           = VARIANT_TYPE_STRING,
  [SRTO_CONGESTION]         = VARIANT_TYPE_STRING,
  [SRTO_MESSAGEAPI]         = VARIANT_TYPE_BOOLEAN,
  [SRTO_PAYLOADSIZE]        = VARIANT_TYPE_INT32,
  [SRTO_TRANSTYPE]          = VARIANT_TYPE_INT32,
  [SRTO_KMREFRESHRATE]      = VARIANT_TYPE_INT32,
  [SRTO_KMPREANNOUNCE]      = VARIANT_TYPE_INT32,
  [SRTO_ENFORCEDENCRYPTION] = VARIANT_TYPE_BOOLEAN,
  [SRTO_IPV6ONLY]           = VARIANT_TYPE_INT32,
  [SRTO_PEERIDLETIMEO]      = VARIANT_TYPE_INT32,
 #if ENABLE_EXPERIMENTAL_BONDING
  [SRTO_GROUPCONNECT]       = VARIANT_TYPE_INT32,
  [SRTO_GROUPSTABTIMEO]     = VARIANT_TYPE_INT32,
  [SRTO_GROUPTYPE]          = VARIANT_TYPE_INT32,
 #endif
  [SRTO_BINDTODEVICE]       = VARIANT_TYPE_STRING,
  [SRTO_PACKETFILTER]       = VARIANT_TYPE_STRING,
  [SRTO_RETRANSMITALGO]     = VARIANT_TYPE_INT32,
};
/* *INDENT-ON* */

typedef union
{
  gint32 i32;
  gint64 i64;
  gboolean bool;
  gchar str[OPT_STR_MAXLEN];
  struct linger linger;
  const gchar *str_ptr;
} OptionValue;

GVariant *
hwangsae_relay_get_socket_option (HwangsaeRelay * relay, SRTSOCKET sock,
    gint option, GError ** error)
{
  OptionValue val;
  const GVariantType *type = srt_options[option].type;
  gint optlen = srt_options[option].optlen;
  g_autoptr (GError) gerror = NULL;

  if (option < 0 || option >= G_N_ELEMENTS (srt_options)) {
    gerror = g_error_new (HWANGSAE_RELAY_ERROR,
        HWANGSAE_RELAY_ERROR_UNKNOWN_SOCKOPT, "Unknown socket option %d",
        option);
    goto error;
  }

  if (srt_getsockflag (sock, option, &val, &optlen) == SRT_ERROR) {
    gerror = g_error_new (HWANGSAE_RELAY_ERROR, HWANGSAE_RELAY_ERROR_SOCKOPT,
        "Error reading socket option %d: %s", option, srt_getlasterror_str ());
    goto error;
  }

  if (g_variant_type_equal (type, G_VARIANT_TYPE_INT32)) {
    return g_variant_new_int32 (val.i32);
  }
  if (g_variant_type_equal (type, G_VARIANT_TYPE_INT64)) {
    return g_variant_new_int64 (val.i64);
  }
  if (g_variant_type_equal (type, G_VARIANT_TYPE_BOOLEAN)) {
    return g_variant_new_boolean (val.bool);
  }
  if (g_variant_type_equal (type, G_VARIANT_TYPE_STRING)) {
    return g_variant_new_string (val.str);
  }
  if (g_variant_type_equal (type, HWANGSAE_VARIANT_TYPE_LINGER)) {
    return g_variant_new ((const gchar *) HWANGSAE_VARIANT_TYPE_LINGER,
        val.linger.l_onoff, val.linger.l_linger);
  }

error:
  if (error) {
    g_propagate_error (error, gerror);
    gerror = NULL;
  }
  return NULL;
}

gboolean
hwangsae_relay_set_socket_option (HwangsaeRelay * relay, SRTSOCKET sock,
    gint option, GVariant * value, GError ** error)
{
  OptionValue val;
  const GVariantType *type = srt_options[option].type;
  gsize optlen = srt_options[option].optlen;
  g_autoptr (GError) gerror = NULL;

  if (option < 0 || option >= G_N_ELEMENTS (srt_options)) {
    gerror = g_error_new (HWANGSAE_RELAY_ERROR,
        HWANGSAE_RELAY_ERROR_UNKNOWN_SOCKOPT, "Unknown socket option %d",
        option);
    goto error;
  }

  if (!g_variant_type_equal (g_variant_get_type (value), type)) {
    gerror = g_error_new (HWANGSAE_RELAY_ERROR,
        HWANGSAE_RELAY_ERROR_INVALID_PARAMETER,
        "Invalid type %s for socket option %d",
        (const gchar *) g_variant_get_type (value), option);
    goto error;
  }

  if (g_variant_type_equal (type, G_VARIANT_TYPE_INT32)) {
    val.i32 = g_variant_get_int32 (value);
  }
  if (g_variant_type_equal (type, G_VARIANT_TYPE_INT64)) {
    val.i64 = g_variant_get_int64 (value);
  }
  if (g_variant_type_equal (type, G_VARIANT_TYPE_BOOLEAN)) {
    val.bool = g_variant_get_boolean (value);
  }
  if (g_variant_type_equal (type, G_VARIANT_TYPE_STRING)) {
    val.str_ptr = g_variant_get_string (value, &optlen);
  }
  if (g_variant_type_equal (type, HWANGSAE_VARIANT_TYPE_LINGER)) {
    g_variant_get (value, (const gchar *) HWANGSAE_VARIANT_TYPE_LINGER,
        &val.linger.l_onoff, &val.linger.l_linger);
  }

  if (srt_setsockflag (sock, option, &val, optlen) == SRT_ERROR) {
    gerror = g_error_new (HWANGSAE_RELAY_ERROR, HWANGSAE_RELAY_ERROR_SOCKOPT,
        "Error setting socket option %d: %s", option, srt_getlasterror_str ());
    goto error;
  }

  return TRUE;

error:
  if (error) {
    g_propagate_error (error, gerror);
    gerror = NULL;
  }
  return FALSE;
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

void
hwangsae_relay_disconnect_sink (HwangsaeRelay * self, const gchar * username)
{
  SinkConnection *sink;

  LOCK_RELAY;

  sink = g_hash_table_lookup (self->username_sink_map, username);
  if (sink) {
    hwangsae_relay_remove_sink (self, sink);
  }
}

void
hwangsae_relay_disconnect_source (HwangsaeRelay * self, const gchar * username,
    const gchar * resource)
{
  GHashTableIter it;
  SinkConnection *sink;

  LOCK_RELAY;

  g_hash_table_iter_init (&it, self->username_sink_map);

  while (g_hash_table_iter_next (&it, NULL, (gpointer *) & sink)) {
    GSList *list;

    if (resource && !g_str_equal (sink->username, resource)) {
      continue;
    }

    list = sink->sources;
    while (list) {
      GSList *next = g_slist_next (list);
      SourceConnection *source = list->data;

      if (g_str_equal (source->username, username)) {
        _sink_connection_remove_source (sink, source);
      }

      list = next;
    }
  }
}
