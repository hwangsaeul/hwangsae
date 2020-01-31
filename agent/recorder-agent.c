/**
 *  Copyright 2019 SK Telecom Co., Ltd.
 *    Author: Walter Lozano <walter.lozano@collabora.com>
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

#include "config.h"

#include "recorder-agent.h"
#include "http-server.h"
#include <chamge/chamge.h>
#include <hwangsae/recorder.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <libsoup/soup.h>
#include <libsoup/soup-message.h>

#include <glib-unix.h>

#include <hwangsae/dbus/manager-generated.h>
#include <hwangsae/dbus/recorder-interface-generated.h>

#define RECORDER_GAEGULI_SRT_MODE GAEGULI_SRT_MODE_CALLER

#define HWANGSAE_RECORDER_SCHEMA_ID "org.hwangsaeul.hwangsae.recorder"

typedef struct
{
  HwangsaeulApplication parent;

  Hwangsae1DBusManager *manager;
  Hwangsae1DBusRecorderInterface *recorder_interface;
  GSettings *settings;
  HwangsaeHttpServer *hwangsae_http_server;

  gchar *recording_dir;
  gchar *relay_address;
  guint relay_api_port;
  guint relay_stream_port;
  gchar *recorder_id;
} HwangsaeRecorderAgentPrivate;

/* *INDENT-OFF* */
G_DEFINE_TYPE_WITH_PRIVATE (HwangsaeRecorderAgent, hwangsae_recorder_agent, HWANGSAEUL_TYPE_APPLICATION)
/* *INDENT-ON* */

enum
{
  PROP_RECORDING_DIR = 1,
  PROP_RELAY_ADDRESS,
  PROP_RELAY_API_PORT,
  PROP_RELAY_STREAM_PORT,
  PROP_RECORDER_ID,
  PROP_LAST
};

void
hwangsae_recorder_agent_send_rest_api (HwangsaeRecorderAgent * self,
    RelayMethods method, gchar * edge_id)
{
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);
  SoupSession *session;
  SoupMessage *msg;
  guint status;
  g_autofree gchar *host = g_strdup (priv->relay_address);
  guint port = priv->relay_api_port;
  g_autofree gchar *url = NULL;
  g_autofree gchar *data = NULL;
  guint data_size = 0;

  if (method == RELAY_METHOD_START_STREAMING) {
    url =
        g_strdup_printf ("http://%s:%d/api/v1.0/srt/%s/%s", host, port, "start",
        edge_id);
    data = g_strdup ("{}");
    data_size = strnlen (data, 32);
  } else if (method == RELAY_METHOD_STOP_STREAMING) {
    url =
        g_strdup_printf ("http://%s:%d/api/v1.0/srt/%s/%s", host, port, "stop",
        edge_id);
    data = g_strdup ("{}");
    data_size = strnlen (data, 32);
  }

  g_debug ("calling api %s", url);
  g_debug ("calling api data %s", data);

  session = soup_session_new ();

  msg = soup_message_new ("POST", url);

  soup_message_set_request (msg, "application/json",
      SOUP_MEMORY_COPY, data, data_size);
  status = soup_session_send_message (session, msg);

  g_debug ("calling api result %d", status);

  g_object_unref (msg);
  g_object_unref (session);

  return;
}

static gint64
hwangsae_recorder_agent_start_recording (HwangsaeRecorderAgent * self,
    gchar * edge_id)
{
  g_debug ("Default implementation");

  return 0;
}

static void
hwangsae_recorder_agent_stop_recording (HwangsaeRecorderAgent * self,
    gchar * edge_id)
{
  g_debug ("Default implementation");
}

static gboolean
hwangsae_recorder_agent_dbus_register (GApplication * app,
    GDBusConnection * connection, const gchar * object_path, GError ** error)
{
  gboolean ret;
  HwangsaeRecorderAgent *self = HWANGSAE_RECORDER_AGENT (app);
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);

  g_debug ("hwangsae_recorder_agent_dbus_register");

  g_application_hold (app);

  /* chain up */
  ret =
      G_APPLICATION_CLASS (hwangsae_recorder_agent_parent_class)->dbus_register
      (app, connection, object_path, error);

  if (ret &&
      !g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON
          (priv->manager), connection,
          "/org/hwangsaeul/Hwangsae1/Manager", error)) {
    g_warning ("Failed to export Hwangsae1 D-Bus interface (reason: %s)",
        (*error)->message);
  }

  if (ret &&
      !g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON
          (priv->recorder_interface), connection,
          "/org/hwangsaeul/Hwangsae1/RecorderInterface", error)) {
    g_warning ("Failed to export Hwangsae1 D-Bus interface (reason: %s)",
        (*error)->message);
  }

  return ret;
}

static void
hwangsae_recorder_agent_dbus_unregister (GApplication * app,
    GDBusConnection * connection, const gchar * object_path)
{
  HwangsaeRecorderAgent *self = HWANGSAE_RECORDER_AGENT (app);
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);

  g_debug ("hwangsae_recorder_agent_dbus_unregister");

  if (priv->manager)
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON
        (priv->manager));

  if (priv->recorder_interface)
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON
        (priv->recorder_interface));

  g_application_release (app);

  /* chain up */
  G_APPLICATION_CLASS (hwangsae_recorder_agent_parent_class)->dbus_unregister
      (app, connection, object_path);
}

/* *INDENT-OFF* */
gboolean
hwangsae_recorder_agent_recorder_interface_handle_start
    (Hwangsae1DBusRecorderInterface * object,
    GDBusMethodInvocation * invocation, gchar * arg_edge_id, gpointer user_data)
/* *INDENT-ON* */

{
  g_autofree gchar *record_id = NULL;
  gint64 rec_id;

  HwangsaeRecorderAgent *self = (HwangsaeRecorderAgent *) user_data;
  HwangsaeRecorderAgentClass *klass = HWANGSAE_RECORDER_AGENT_GET_CLASS (self);

  g_debug ("hwangsae_recorder_agent_recorder_interface_handle_start");

  rec_id = klass->start_recording (self, arg_edge_id);

  if (rec_id > 0)
    record_id = g_strdup_printf ("%ld", rec_id);
  else
    record_id = g_strdup ("");

  hwangsae1_dbus_recorder_interface_complete_start (object, invocation,
      record_id);

  return TRUE;
}

/* *INDENT-OFF* */
gboolean
hwangsae_recorder_agent_recorder_interface_handle_stop
    (Hwangsae1DBusRecorderInterface * object,
    GDBusMethodInvocation * invocation, gchar * arg_edge_id, gpointer user_data)
/* *INDENT-ON* */

{
  HwangsaeRecorderAgent *self = (HwangsaeRecorderAgent *) user_data;
  HwangsaeRecorderAgentClass *klass = HWANGSAE_RECORDER_AGENT_GET_CLASS (self);

  g_debug ("hwangsae_recorder_agent_recorder_interface_handle_stop");

  klass->stop_recording (self, arg_edge_id);

  hwangsae1_dbus_recorder_interface_complete_stop (object, invocation);

  return TRUE;
}

gint
find_chr (const gchar * str, gchar c)
{
  gint pos = -1;
  gchar *p = NULL;
  p = strchr (str, c);
  if (p)
    pos = p - str;
  return pos;
}

static void
parse_filename (const gchar * fn, gchar ** record_id, gint64 * file_start,
    gint64 * file_end)
{
  g_autofree gchar *tmp = NULL;
  g_autofree gchar *file_start_str = NULL;
  g_autofree gchar *file_end_str = NULL;
  gchar **parts = NULL;

  *file_start = 0;
  *file_end = 0;

  parts = g_strsplit (fn, ".", -1);
  if (!(parts && parts[0] && parts[1]))
    goto cleanup;

  tmp = g_strdup (parts[0]);

  g_strfreev (parts);

  parts = g_strsplit (tmp, "-", -1);
  if (!parts[0] || g_strcmp0 (parts[0], "hwangsae"))
    goto cleanup;
  if (!parts[1] || g_strcmp0 (parts[1], "recording"))
    goto cleanup;
  if (!parts[2] || !parts[3])
    goto cleanup;

  *record_id = g_strdup (parts[2]);
  file_start_str = g_strdup (parts[3]);
  file_end_str = g_strdup (parts[4]);

  *file_start = g_ascii_strtoll (file_start_str, NULL, 10);
  *file_end = g_ascii_strtoll (file_end_str, NULL, 10);

cleanup:
  g_strfreev (parts);
}

gint
gchar_compare (gconstpointer a, gconstpointer b)
{
  gchar *str_a = *(gchar **) a;
  gchar *str_b = *(gchar **) b;

  return g_strcmp0 (str_a, str_b);
}

static gchar *
get_records (gchar * recording_dir, gchar * arg_edge_id, gchar * arg_record_id,
    gint64 arg_from, gint64 arg_to, GVariantBuilder * builder)
{
  g_autoptr (GDir) dir = NULL;
  g_autoptr (GError) error = NULL;
  const gchar *filename = NULL;
  gchar *edge_id = NULL;
  gboolean records_found = FALSE;

  if ((!arg_edge_id || !g_strcmp0 (arg_edge_id, "")) &&
      (!arg_record_id || !g_strcmp0 (arg_record_id, ""))) {
    edge_id = g_strdup ("");

    return edge_id;
  }

  dir = g_dir_open (recording_dir, 0, &error);
  if (error)
    return NULL;

  while ((filename = g_dir_read_name (dir)) && !records_found) {
    g_autofree gchar *recording_edge_dir = NULL;
    g_autofree gchar *edge_id_tmp = NULL;
    g_autoptr (GArray) file_list = NULL;
    g_autoptr (GDir) subdir = NULL;
    g_autoptr (GError) error2 = NULL;

    edge_id_tmp = g_strdup (filename);
    recording_edge_dir = g_build_filename (recording_dir, filename, NULL);
    if (arg_edge_id && g_strcmp0 (arg_edge_id, "")
        && g_strcmp0 (arg_edge_id, edge_id_tmp))
      continue;
    if (!g_file_test (recording_edge_dir, G_FILE_TEST_IS_DIR))
      continue;

    subdir = g_dir_open (recording_edge_dir, 0, &error2);
    if (error2)
      continue;

    file_list = g_array_new (TRUE, TRUE, sizeof (gchar *));
    while ((filename = g_dir_read_name (subdir))) {
      g_array_append_val (file_list, filename);
    }

    g_array_sort (file_list, gchar_compare);

    for (gint i = 0; i < file_list->len; i++) {
      gint64 file_start = 0;
      gint64 file_end = 0;
      g_autofree gchar *record_id = NULL;
      g_autofree gchar *file_id = NULL;
      g_autofree gchar *filename_full = NULL;
      GStatBuf st;

      filename = g_array_index (file_list, gchar *, i);

      parse_filename (filename, &record_id, &file_start, &file_end);
      if (!record_id || file_start <= 0 || file_end <= 0)
        continue;

      if (arg_record_id && g_strcmp0 (arg_record_id, "")
          && g_strcmp0 (arg_record_id, record_id))
        continue;

      records_found = TRUE;

      file_id = g_strdup_printf ("%s-%ld-%ld", record_id, file_start, file_end);

      filename_full = g_build_filename (recording_edge_dir, filename, NULL);
      g_stat (filename_full, &st);

      if (arg_to == 0)
        arg_to = G_MAXINT64;

      if (!(file_end < arg_from || file_start > arg_to)) {

        if (!edge_id)
          edge_id = g_strdup (edge_id_tmp);

        if (!arg_edge_id)
          g_variant_builder_add (builder, "(sxxx)", file_id,
              file_start, file_end, st.st_size);
        else if (!arg_record_id) {
          g_variant_builder_add (builder, "(ssxxx)", record_id,
              file_id, file_start, file_end, st.st_size);
        }
      }
    }
  }

  if (!edge_id)
    edge_id = g_strdup ("");

  return edge_id;
}

/* *INDENT-OFF* */
gboolean
hwangsae_recorder_agent_recorder_interface_handle_lookup_by_record
    (Hwangsae1DBusRecorderInterface * object,
    GDBusMethodInvocation * invocation, gchar * arg_record_id, gint64 arg_from,
    gint64 arg_to, gpointer user_data)
/* *INDENT-ON* */

{
  HwangsaeRecorderAgent *self = (HwangsaeRecorderAgent *) user_data;
  g_autofree gchar *recording_dir = NULL;
  g_autofree gchar *edge_id = NULL;
  g_autoptr (GVariantBuilder) builder = NULL;
  GVariant *records = NULL;
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);

  g_debug
      ("hwangsae_recorder_agent_recorder_interface_handle_lookup_by_record");

  recording_dir = g_strdup (priv->recording_dir);

  builder = g_variant_builder_new (G_VARIANT_TYPE ("a(sxxx)"));

  edge_id = get_records (recording_dir, NULL, arg_record_id, arg_from, arg_to,
      builder);

  records = g_variant_builder_end (builder);

  hwangsae1_dbus_recorder_interface_complete_lookup_by_record (object,
      invocation, edge_id, records);

  return TRUE;
}

/* *INDENT-OFF* */
gboolean
hwangsae_recorder_agent_recorder_interface_handle_lookup_by_edge
    (Hwangsae1DBusRecorderInterface * object,
    GDBusMethodInvocation * invocation, gchar * arg_edge_id, gint64 arg_from,
    gint64 arg_to, gpointer user_data)
/* *INDENT-ON* */

{
  HwangsaeRecorderAgent *self = (HwangsaeRecorderAgent *) user_data;
  g_autofree gchar *recording_dir = NULL;
  g_autofree gchar *edge_id = NULL;
  g_autoptr (GVariantBuilder) builder = NULL;
  GVariant *records = NULL;
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);


  g_debug ("hwangsae_recorder_agent_recorder_interface_handle_lookup_by_edge");

  recording_dir = g_strdup (priv->recording_dir);

  builder = g_variant_builder_new (G_VARIANT_TYPE ("a(ssxxx)"));

  edge_id = get_records (recording_dir, arg_edge_id, NULL, arg_from, arg_to,
      builder);

  records = g_variant_builder_end (builder);

  hwangsae1_dbus_recorder_interface_complete_lookup_by_edge (object,
      invocation, records);

  return TRUE;
}

/* *INDENT-OFF* */
gboolean
hwangsae_recorder_agent_recorder_interface_handle_url
    (Hwangsae1DBusRecorderInterface * object,
    GDBusMethodInvocation * invocation, gchar * arg_edge_id,
    gchar * arg_file_id, gpointer user_data)
/* *INDENT-ON* */

{
  HwangsaeRecorderAgent *self = (HwangsaeRecorderAgent *) user_data;
  g_autofree gchar *url = NULL;
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);

  g_debug ("hwangsae_recorder_agent_recorder_interface_handle_url");

  url = hwangsae_http_server_get_url (priv->hwangsae_http_server, arg_edge_id,
      arg_file_id);

  g_debug ("url: %s", url);

  hwangsae1_dbus_recorder_interface_complete_url (object, invocation, url);

  return TRUE;
}

/* *INDENT-OFF* */
gboolean
hwangsae_recorder_agent_recorder_interface_handle_delete
    (Hwangsae1DBusRecorderInterface * object,
    GDBusMethodInvocation * invocation, gchar * arg_edge_id,
    gchar * arg_file_id, gpointer user_data)
/* *INDENT-ON* */

{
  HwangsaeRecorderAgent *self = (HwangsaeRecorderAgent *) user_data;
  g_autofree gchar *filename = NULL;
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);

  g_debug ("hwangsae_recorder_agent_recorder_interface_handle_delete");

  filename =
      hwangsae_http_server_check_file_path (priv->hwangsae_http_server,
      arg_edge_id, arg_file_id);

  if (filename) {
    g_debug ("deleting file %s", filename);
    g_remove (filename);
  } else {
    g_debug ("unable to delete file id %s, not found", arg_file_id);
  }

  hwangsae1_dbus_recorder_interface_complete_delete (object, invocation);

  return TRUE;
}

gchar *
hwangsae_recorder_agent_get_recorder_id (HwangsaeRecorderAgent * self)
{
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);

  return g_strdup (priv->recorder_id);
}

gchar *
hwangsae_recorder_agent_get_recording_dir (HwangsaeRecorderAgent * self)
{
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);

  return g_strdup (priv->recording_dir);
}

gchar *
hwangsae_recorder_agent_get_relay_address (HwangsaeRecorderAgent * self)
{
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);

  return g_strdup (priv->relay_address);
}

guint
hwangsae_recorder_agent_get_relay_stream_port (HwangsaeRecorderAgent * self)
{
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);

  return priv->relay_stream_port;
}

static void
hwangsae_recorder_agent_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  HwangsaeRecorderAgent *self = HWANGSAE_RECORDER_AGENT (object);
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);

  switch (property_id) {
    case PROP_RECORDING_DIR:{
      const gchar *str = g_value_get_string (value);

      g_clear_pointer (&priv->recording_dir, g_free);
      if (str && (str[0] != '\0')) {
        priv->recording_dir = g_strdup (str);
      } else {
        priv->recording_dir = g_build_filename (g_get_user_data_dir (),
            "hwangsaeul", "hwangsae", "recordings", NULL);
      }

      if (priv->hwangsae_http_server) {
        g_object_set (priv->hwangsae_http_server, "recording-dir",
            priv->recording_dir, NULL);
      }
      break;
    }
    case PROP_RELAY_ADDRESS:
      g_clear_pointer (&priv->relay_address, g_free);
      priv->relay_address = g_strdup (g_value_get_string (value));
      break;
    case PROP_RELAY_API_PORT:
      priv->relay_api_port = g_value_get_uint (value);
      break;
    case PROP_RELAY_STREAM_PORT:
      priv->relay_stream_port = g_value_get_uint (value);
      break;
    case PROP_RECORDER_ID:
      g_clear_pointer (&priv->recorder_id, g_free);
      priv->recorder_id = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
hwangsae_recorder_agent_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  HwangsaeRecorderAgent *self = HWANGSAE_RECORDER_AGENT (object);
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);

  switch (property_id) {
    case PROP_RECORDING_DIR:
      g_value_set_string (value, priv->recording_dir);
      break;
    case PROP_RELAY_ADDRESS:
      g_value_set_string (value, priv->relay_address);
      break;
    case PROP_RELAY_API_PORT:
      g_value_set_uint (value, priv->relay_api_port);
      break;
    case PROP_RELAY_STREAM_PORT:
      g_value_set_uint (value, priv->relay_stream_port);
      break;
    case PROP_RECORDER_ID:
      g_value_set_string (value, priv->recorder_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
hwangsae_recorder_agent_dispose (GObject * object)
{
  HwangsaeRecorderAgent *self = HWANGSAE_RECORDER_AGENT (object);
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);

  g_clear_object (&priv->settings);
  g_clear_object (&priv->manager);
  g_clear_object (&priv->recorder_interface);
  g_clear_object (&priv->hwangsae_http_server);

  G_OBJECT_CLASS (hwangsae_recorder_agent_parent_class)->dispose (object);
}

static void
hwangsae_recorder_agent_class_init (HwangsaeRecorderAgentClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);
  HwangsaeRecorderAgentClass *recorder_agent_class =
      HWANGSAE_RECORDER_AGENT_CLASS (klass);

  gobject_class->set_property = hwangsae_recorder_agent_set_property;
  gobject_class->get_property = hwangsae_recorder_agent_get_property;
  gobject_class->dispose = hwangsae_recorder_agent_dispose;

  g_object_class_install_property (gobject_class, PROP_RECORDING_DIR,
      g_param_spec_string ("recording-dir", "Recording Directory",
          "Recording Directory", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RELAY_ADDRESS,
      g_param_spec_string ("relay-address", "Relay IP address",
          "Relay IP address", "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RELAY_API_PORT,
      g_param_spec_uint ("relay-api-port", "Relay TCP API port",
          "Relay TCP API port", 0, G_MAXUINT, 8080,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RELAY_STREAM_PORT,
      g_param_spec_uint ("relay-stream-port", "Relay Stream port",
          "Relay Stream port", 0, G_MAXUINT, 9999,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RECORDER_ID,
      g_param_spec_string ("recorder-id", "Recorder ID",
          "Recorder ID", "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  app_class->dbus_register = hwangsae_recorder_agent_dbus_register;
  app_class->dbus_unregister = hwangsae_recorder_agent_dbus_unregister;

  recorder_agent_class->start_recording =
      hwangsae_recorder_agent_start_recording;
  recorder_agent_class->stop_recording = hwangsae_recorder_agent_stop_recording;

}

static void
hwangsae_recorder_agent_init (HwangsaeRecorderAgent * self)
{
  HwangsaeRecorderAgentPrivate *priv =
      hwangsae_recorder_agent_get_instance_private (self);

  priv->settings = chamge_common_gsettings_new (HWANGSAE_RECORDER_SCHEMA_ID);

  priv->hwangsae_http_server =
      hwangsae_http_server_new (g_settings_get_uint (priv->settings,
          "http-port"));

  g_settings_bind (priv->settings, "recording-dir", self, "recording-dir",
      G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (priv->settings, "relay-address", self, "relay-address",
      G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (priv->settings, "relay-api-port", self, "relay-api-port",
      G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (priv->settings, "relay-stream-port", self,
      "relay-stream-port", G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (priv->settings, "recorder-id", self, "recorder-id",
      G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (priv->settings, "external-ip", priv->hwangsae_http_server,
      "external-ip", G_SETTINGS_BIND_DEFAULT);

  if (!g_strcmp0 (priv->recorder_id, "randomized-string")
      || strnlen (priv->recorder_id, 64) == 0) {
    g_autofree gchar *uid = g_uuid_string_random ();
    g_autofree gchar *recorder_id =
        g_compute_checksum_for_string (G_CHECKSUM_SHA256, uid, strnlen (uid,
            64));
    g_object_set (priv, "recorder-id", recorder_id, NULL);
  }

  priv->manager = hwangsae1_dbus_manager_skeleton_new ();

  hwangsae1_dbus_manager_set_status (priv->manager, 1);

  priv->recorder_interface = hwangsae1_dbus_recorder_interface_skeleton_new ();

  g_signal_connect (priv->recorder_interface, "handle-start",
      G_CALLBACK (hwangsae_recorder_agent_recorder_interface_handle_start),
      self);

  g_signal_connect (priv->recorder_interface, "handle-stop",
      G_CALLBACK (hwangsae_recorder_agent_recorder_interface_handle_stop),
      self);

  g_signal_connect (priv->recorder_interface, "handle-lookup-by-record",
      G_CALLBACK
      (hwangsae_recorder_agent_recorder_interface_handle_lookup_by_record),
      self);

  g_signal_connect (priv->recorder_interface, "handle-lookup-by-edge",
      G_CALLBACK
      (hwangsae_recorder_agent_recorder_interface_handle_lookup_by_edge), self);

  g_signal_connect (priv->recorder_interface, "handle-url",
      G_CALLBACK (hwangsae_recorder_agent_recorder_interface_handle_url), self);

  g_signal_connect (priv->recorder_interface, "handle-delete",
      G_CALLBACK
      (hwangsae_recorder_agent_recorder_interface_handle_delete), self);
}
