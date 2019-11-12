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

#include <srt/srt.h>
#include <gio/gio.h>

const guint32 SRT_BACKLOG_LEN = 100;
const gint MAX_EPOLL_SRT_SOCKETS = 4000;
const int64_t MAX_EPOLL_WAIT_TIMEOUT_MS = 100;
const gint SRT_POLL_EVENTS = SRT_EPOLL_IN | SRT_EPOLL_ERR;

typedef struct
{
  SRTSOCKET socket;
  GSList *sources;
} SinkConnection;

struct _HwangsaeRelay
{
  GObject parent;

  GMutex lock;

  GSettings *settings;

  guint sink_port;
  guint source_port;

  SRTSOCKET sink_listen_sock;
  SRTSOCKET source_listen_sock;

  SinkConnection *sink;
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
hwangsae_relay_remove_source (HwangsaeRelay * self, SRTSOCKET sink_socket,
    SRTSOCKET source_socket)
{
  g_assert (self->sink && sink_socket == self->sink->socket);

  g_debug ("Closing source connection %d", source_socket);

  self->sink->sources = g_slist_remove (self->sink->sources,
      GINT_TO_POINTER (source_socket));
  srt_close (source_socket);
}

static void
hwangsae_relay_remove_sink (HwangsaeRelay * self, SRTSOCKET sink_socket)
{
  g_assert (self->sink && sink_socket == self->sink->socket);

  g_debug ("Closing sink connection %d", sink_socket);

  while (self->sink->sources) {
    hwangsae_relay_remove_source (self, self->sink->socket,
        GPOINTER_TO_INT (self->sink->sources->data));
  }

  srt_close (self->sink->socket);
  g_clear_pointer (&self->sink, g_free);
}

static void
hwangsae_relay_finalize (GObject * object)
{
  HwangsaeRelay *self = HWANGSAE_RELAY (object);

  self->run_relay_thread = FALSE;
  g_clear_pointer (&self->relay_thread, g_thread_join);

  g_mutex_clear (&self->lock);

  srt_close (self->sink_listen_sock);
  srt_close (self->source_listen_sock);

  if (self->sink) {
    hwangsae_relay_remove_sink (self, self->sink->socket);
  }

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
  g_autofree gchar *username = NULL;

  LOCK_RELAY;

  if (self->sink) {
    // We already have a sink connected.
    return -1;
  }

  _parse_stream_id (stream_id, &username, NULL);
  if (!username) {
    // Sink socket must have username in its Stream ID.
    return -1;
  }

  g_debug ("Accepting sink %d username: %s", sock, username);

  self->sink = g_new0 (SinkConnection, 1);
  self->sink->socket = sock;
  srt_epoll_add_usock (self->poll_id, sock, &SRT_POLL_EVENTS);

  return 0;
}

static gint
hwangsae_relay_accept_source (HwangsaeRelay * self, SRTSOCKET sock,
    gint hs_version, const struct sockaddr *peeraddr, const gchar * stream_id)
{
  LOCK_RELAY;

  if (!self->sink) {
    // We have no sink.
    return -1;
  }

  g_debug ("Accepting source %d", sock);

  self->sink->sources = g_slist_append (self->sink->sources,
      GINT_TO_POINTER (sock));

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
        gint recv;

        LOCK_RELAY;

        if (rsocket == self->sink_listen_sock ||
            rsocket == self->source_listen_sock) {
          /* We already added the socket to our internal structures in the
           * accept callback, so only finalize its creation with srt_accept
           * here */
          srt_accept (rsocket, NULL, NULL);
        } else {
          do {
            recv = srt_recv (rsocket, buf, sizeof (buf));

            if (recv > 0) {
              GSList *it = self->sink->sources;

              while (it) {
                SRTSOCKET source_socket = GPOINTER_TO_INT (it->data);

                it = it->next;

                if (srt_send (source_socket, buf, recv) < 0) {
                  gint error = srt_getlasterror (NULL);
                  if (error == SRT_ECONNLOST) {
                    hwangsae_relay_remove_source (self, rsocket, source_socket);
                  } else {
                    g_debug ("srt_send failed %s", srt_strerror (error, 0));
                  }
                }
              }
            } else if (recv < 0) {
              gint error = srt_getlasterror (NULL);
              if (error == SRT_ECONNLOST) {
                hwangsae_relay_remove_sink (self, rsocket);
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

  self->poll_id = srt_epoll_create ();

  self->sink_listen_sock = _srt_open_listen_sock (self->sink_port);
  srt_listen_callback (self->sink_listen_sock,
      (srt_listen_callback_fn *) hwangsae_relay_accept_sink, self);
  srt_epoll_add_usock (self->poll_id, self->sink_listen_sock, &SRT_POLL_EVENTS);

  self->source_listen_sock = _srt_open_listen_sock (self->source_port);
  srt_listen_callback (self->source_listen_sock,
      (srt_listen_callback_fn *) hwangsae_relay_accept_source, self);
  srt_epoll_add_usock (self->poll_id, self->source_listen_sock,
      &SRT_POLL_EVENTS);

  LOCK_RELAY;
  self->run_relay_thread = TRUE;
  self->relay_thread = g_thread_new ("HwangsaeRelay", _relay_main, self);
}

HwangsaeRelay *
hwangsae_relay_new (void)
{
  return g_object_new (HWANGSAE_TYPE_RELAY, NULL);
}
