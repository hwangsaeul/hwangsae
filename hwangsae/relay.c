/**
 *  Copyright 2019 SK Telecom Co., Ltd.
 *    Author: Jakub Adam <jakub.adam@collabora.com>
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

#include <ifaddrs.h>
#include <net/if.h>
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

static void
_sink_connection_remove_source (SinkConnection * sink, SRTSOCKET source)
{
  g_debug ("Closing source connection %d", source);
  srt_close (source);
  sink->sources = g_slist_remove (sink->sources, GINT_TO_POINTER (source));
}

static void
_sink_connection_free (SinkConnection * sink)
{
  while (sink->sources) {
    _sink_connection_remove_source (sink,
        GPOINTER_TO_INT (sink->sources->data));
  }

  g_debug ("Closing sink connection %d", sink->socket);
  srt_close (sink->socket);
  g_clear_pointer (&sink->username, g_free);
  g_free (sink);
}

struct _HwangsaeRelay
{
  GObject parent;

  GMutex lock;

  GSettings *settings;

  guint sink_port;
  guint source_port;
  gchar *external_ip;

  gchar *sink_uri;
  gchar *source_uri;

  SRTSOCKET sink_listen_sock;
  SRTSOCKET source_listen_sock;

  GHashTable *srtsocket_sink_map;
  GHashTable *username_sink_map;
  int poll_id;

  GThread *relay_thread;
  gboolean run_relay_thread;
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
  PROP_LAST
};

typedef struct
{
  const gchar *name;
  gint param;
  gint val;
} SrtParam;

static SrtParam srt_params[] = {
  {"SRTO_SNDSYN", SRTO_SNDSYN, 0},      /* 0: non-blocking */
  {"SRTO_RCVSYN", SRTO_RCVSYN, 0},      /* 0: non-blocking */
  {"SRTO_LINGER", SRTO_LINGER, 0},
  {"SRTO_TSBPMODE", SRTO_TSBPDMODE, 1}, /* Timestamp-based Packet Delivery */
  {"SRTO_RENDEZVOUS", SRTO_RENDEZVOUS, 0},      /* 0: not for rendezvous */
  {NULL, -1, -1},
};

static void
hwangsae_relay_remove_sink (HwangsaeRelay * self, SinkConnection * sink)
{
  g_hash_table_remove (self->username_sink_map, sink->username);
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

  g_hash_table_destroy (self->srtsocket_sink_map);
  g_hash_table_destroy (self->username_sink_map);

  g_clear_handle_id (&self->poll_id, srt_epoll_release);
  g_clear_object (&self->settings);

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
}

static void
_parse_stream_id (const gchar * stream_id, gchar ** username, gchar ** resource)
{
  const gchar STREAM_ID_PREFIX[] = "#!::";
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

static gint
hwangsae_relay_accept_sink (HwangsaeRelay * self, SRTSOCKET sock,
    gint hs_version, const struct sockaddr *peeraddr, const gchar * stream_id)
{
  gchar *username = NULL;
  SinkConnection *sink;

  LOCK_RELAY;

  _parse_stream_id (stream_id, &username, NULL);
  if (!username) {
    // Sink socket must have username in its Stream ID.
    return -1;
  }

  if (g_hash_table_contains (self->username_sink_map, username)) {
    /* Sink already registered. */
    return -1;
  }

  g_debug ("Accepting sink %d username: %s", sock, username);

  sink = g_new0 (SinkConnection, 1);
  sink->socket = sock;
  sink->username = username;

  g_hash_table_insert (self->srtsocket_sink_map, &sink->socket, sink);
  g_hash_table_insert (self->username_sink_map, sink->username, sink);

  srt_epoll_add_usock (self->poll_id, sock, &SRT_POLL_EVENTS);

  return 0;
}

static gboolean
_find_first_value (gpointer key, gpointer value, gpointer user_data)
{
  return TRUE;
}

static gint
hwangsae_relay_accept_source (HwangsaeRelay * self, SRTSOCKET sock,
    gint hs_version, const struct sockaddr *peeraddr, const gchar * stream_id)
{
  g_autofree gchar *resource = NULL;
  SinkConnection *sink;

  LOCK_RELAY;

  if (g_hash_table_size (self->srtsocket_sink_map) == 0) {
    /* We have no sinks. */
    return -1;
  }

  _parse_stream_id (stream_id, NULL, &resource);

  g_debug ("Accepting source %d", sock);

  if (resource) {
    sink = g_hash_table_lookup (self->username_sink_map, resource);
  } else {
    /*
     * Pick the first available sink.
     * TODO: Reject sources without 'r' key in Stream ID.
     */
    sink =
        g_hash_table_find (self->srtsocket_sink_map, _find_first_value, NULL);
  }

  if (!sink) {
    /* Reject the attempt to connect an unknown sink. */
    return -1;
  }

  sink->sources = g_slist_append (sink->sources, GINT_TO_POINTER (sock));

  return 0;
}

static gpointer
_relay_main (gpointer data)
{
  HwangsaeRelay *self = HWANGSAE_RELAY (data);
  SRTSOCKET readfds[MAX_EPOLL_SRT_SOCKETS];
  gchar buf[1400];

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

        if (rsocket == self->sink_listen_sock ||
            rsocket == self->source_listen_sock) {
          /* We already added the socket to our internal structures in the
           * accept callback, so only finalize its creation with srt_accept
           * here */
          srt_accept (rsocket, NULL, NULL);
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
                SRTSOCKET source_socket = GPOINTER_TO_INT (it->data);

                it = it->next;

                if (srt_send (source_socket, buf, recv) < 0) {
                  gint error = srt_getlasterror (NULL);
                  if (error == SRT_ECONNLOST) {
                    _sink_connection_remove_source (sink, source_socket);
                  } else {
                    g_debug ("srt_send failed %s", srt_strerror (error, 0));
                  }
                }
              }
            } else if (recv < 0) {
              gint error = srt_getlasterror (NULL);
              if (error == SRT_ECONNLOST) {
                hwangsae_relay_remove_sink (self, sink);
                break;
              } else if (error != SRT_EASYNCRCV) {
                g_debug ("srt_recv error %s", srt_strerror (error, 0));
              }
            }
          } while (recv > 0);
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

  self->settings = g_settings_new ("org.hwangsaeul.hwangsae.relay");

  g_settings_bind (self->settings, "sink-port", self, "sink-port",
      G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "source-port", self, "source-port",
      G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "external-ip", self, "external-ip",
      G_SETTINGS_BIND_DEFAULT);

  self->poll_id = srt_epoll_create ();

  self->sink_listen_sock = _srt_open_listen_sock (self->sink_port);
  srt_listen_callback (self->sink_listen_sock,
      (srt_listen_callback_fn *) hwangsae_relay_accept_sink, self);
  srt_epoll_add_usock (self->poll_id, self->sink_listen_sock, &SRT_POLL_EVENTS);

  g_debug ("URI for sink connection is %s", hwangsae_relay_get_sink_uri (self));

  self->source_listen_sock = _srt_open_listen_sock (self->source_port);
  srt_listen_callback (self->source_listen_sock,
      (srt_listen_callback_fn *) hwangsae_relay_accept_source, self);
  srt_epoll_add_usock (self->poll_id, self->source_listen_sock,
      &SRT_POLL_EVENTS);

  self->srtsocket_sink_map = g_hash_table_new_full (g_int_hash, g_int_equal,
      NULL, (GDestroyNotify) _sink_connection_free);
  self->username_sink_map = g_hash_table_new (g_str_hash, g_str_equal);

  LOCK_RELAY;
  self->run_relay_thread = TRUE;
  self->relay_thread = g_thread_new ("HwangsaeRelay", _relay_main, self);
}

HwangsaeRelay *
hwangsae_relay_new (void)
{
  return g_object_new (HWANGSAE_TYPE_RELAY, NULL);
}

static gchar *
_get_local_ip (void)
{
  struct ifaddrs *addrs;
  struct ifaddrs *it;
  gchar *result = NULL;

  if (getifaddrs (&addrs) < 0) {
    return NULL;
  }

  for (it = addrs; !result && it; it = it->ifa_next) {
    socklen_t addr_len;
    char buf[INET6_ADDRSTRLEN + 1];

    /* Ignore interfaces that are down, not running or don't have an IP. We also
     * want to skip loopbacks. */
    if ((it->ifa_flags & (IFF_UP | IFF_RUNNING | IFF_LOOPBACK)) !=
        (IFF_UP | IFF_RUNNING) || it->ifa_addr == NULL) {
      continue;
    }

    switch (it->ifa_addr->sa_family) {
      case AF_INET:
        addr_len = sizeof (struct sockaddr_in);
        break;
      case AF_INET6:
        addr_len = sizeof (struct sockaddr_in6);
        break;
      default:
        continue;
    }

    if (getnameinfo (it->ifa_addr, addr_len, buf, sizeof (buf), NULL, 0,
            NI_NUMERICHOST) != 0) {
      continue;
    }

    result = g_strdup (buf);
  }

  freeifaddrs (addrs);

  return result;
}

static gchar *
hwangsae_relay_make_uri (HwangsaeRelay * self, guint port)
{
  g_autofree gchar *local_ip = NULL;
  const gchar *ip;

  if (self->external_ip) {
    ip = self->external_ip;
  } else {
    ip = local_ip = _get_local_ip ();
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
