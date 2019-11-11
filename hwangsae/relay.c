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
hwangsae_relay_finalize (GObject * object)
{
  HwangsaeRelay *self = HWANGSAE_RELAY (object);

  g_mutex_clear (&self->lock);

  srt_close (self->sink_listen_sock);
  srt_close (self->source_listen_sock);

  if (self->sink) {
    g_slist_foreach (self->sink->sources, (GFunc) srt_close, NULL);
    srt_close (self->sink->socket);
    g_slist_free (self->sink->sources);
    g_clear_pointer (&self->sink, g_free);
  }

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

static gint
hwangsae_relay_accept_sink (HwangsaeRelay * self, SRTSOCKET sock,
    gint hs_version, const struct sockaddr *peeraddr, const gchar * stream_id)
{
  LOCK_RELAY;

  if (self->sink) {
    // We already have a sink connected.
    return -1;
  }

  g_debug ("Accepting sink %d", sock);

  self->sink = g_new0 (SinkConnection, 1);
  self->sink->socket = sock;

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

  self->sink_listen_sock = _srt_open_listen_sock (self->sink_port);
  srt_listen_callback (self->sink_listen_sock,
      (srt_listen_callback_fn *) hwangsae_relay_accept_sink, self);
  self->source_listen_sock = _srt_open_listen_sock (self->source_port);
  srt_listen_callback (self->source_listen_sock,
      (srt_listen_callback_fn *) hwangsae_relay_accept_source, self);
}

HwangsaeRelay *
hwangsae_relay_new (void)
{
  return g_object_new (HWANGSAE_TYPE_RELAY, NULL);
}
