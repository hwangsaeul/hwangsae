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

struct _HwangsaeRecorderAgent
{
  GApplication parent;

  HwangsaeRecorder *recorder;
  Hwangsae1DBusManager *manager;
  Hwangsae1DBusRecorderInterface *recorder_interface;
  GSettings *settings;
  HwangsaeHttpServer *hwangsae_http_server;

  gboolean is_recording;
  gint64 recording_id;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (HwangsaeRecorderAgent, hwangsae_recorder_agent, G_TYPE_APPLICATION)
/* *INDENT-ON* */

typedef enum
{
  RELAY_METHOD_NONE = 0,
  RELAY_METHOD_START_STREAMING,
  RELAY_METHOD_STOP_STREAMING
} relay_methods;

static void
hwangsae_recorder_agent_send_rest_api (HwangsaeRecorderAgent * self,
    relay_methods method, gchar * edge_id)
{
  SoupSession *session;
  SoupMessage *msg;
  guint status;
  g_autofree gchar *host =
      g_settings_get_string (self->settings, "relay-address");
  guint port = g_settings_get_uint (self->settings, "relay-api-port");
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
  g_autoptr (GError) error = NULL;
  g_autofree gchar *host =
      g_settings_get_string (self->settings, "relay-address");
  guint port = g_settings_get_uint (self->settings, "relay-stream-port");
  g_autofree gchar *streamid = NULL;
  g_autofree gchar *url = NULL;
  g_autofree gchar *recording_edge_dir = NULL;
  g_autofree gchar *filename_prefix = NULL;

  if (self->is_recording) {
    g_warning ("recording already started");
    return self->recording_id;
  }

  self->is_recording = TRUE;
  self->recording_id = g_get_real_time ();

  hwangsae_recorder_agent_send_rest_api (self, RELAY_METHOD_START_STREAMING,
      edge_id);

  streamid = g_strdup_printf ("#!::r=%s", edge_id);
  streamid = g_uri_escape_string (streamid, NULL, FALSE);
  url = g_strdup_printf ("srt://%s:%d?streamid=%s", host, port, streamid);

  g_debug ("starting to recording stream from %s", url);

  g_autofree gchar *recording_dir =
      g_settings_get_string (self->settings, "recording-dir");
  if (g_str_equal (recording_dir, "")) {
    g_free (recording_dir);
    recording_dir = g_build_filename (g_get_user_data_dir (),
        "hwangsaeul", "hwangsae", "recordings", NULL);
  }

  recording_edge_dir = g_build_filename (recording_dir, edge_id, NULL);

  g_debug ("setting recording_dir: %s\n", recording_edge_dir);

  filename_prefix = g_strdup_printf ("hwangsae-recording-%ld",
      self->recording_id);

  g_object_set (self->recorder, "recording-dir", recording_edge_dir,
      "filename-prefix", filename_prefix, NULL);

  hwangsae_recorder_start_recording (self->recorder, url);

  return self->recording_id;
}

static void
hwangsae_recorder_agent_stop_recording (HwangsaeRecorderAgent * self,
    gchar * edge_id)
{
  g_autoptr (GError) error = NULL;

  if (!self->is_recording) {
    g_warning ("recording already stopped");
    return;
  }

  self->is_recording = FALSE;

  hwangsae_recorder_stop_recording (self->recorder);
}

static gboolean
hwangsae_recorder_agent_dbus_register (GApplication * app,
    GDBusConnection * connection, const gchar * object_path, GError ** error)
{
  gboolean ret;
  HwangsaeRecorderAgent *self = HWANGSAE_RECORDER_AGENT (app);

  g_debug ("hwangsae_recorder_agent_dbus_register");

  g_application_hold (app);

  /* chain up */
  ret =
      G_APPLICATION_CLASS (hwangsae_recorder_agent_parent_class)->dbus_register
      (app, connection, object_path, error);

  if (ret &&
      !g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON
          (self->manager), connection,
          "/org/hwangsaeul/Hwangsae1/Manager", error)) {
    g_warning ("Failed to export Hwangsae1 D-Bus interface (reason: %s)",
        (*error)->message);
  }

  if (ret &&
      !g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON
          (self->recorder_interface), connection,
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

  g_debug ("hwangsae_recorder_agent_dbus_unregister");

  if (self->manager)
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON
        (self->manager));

  if (self->recorder_interface)
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON
        (self->recorder_interface));

  g_application_release (app);

  /* chain up */
  G_APPLICATION_CLASS (hwangsae_recorder_agent_parent_class)->dbus_unregister
      (app, connection, object_path);
}

gboolean
    hwangsae_recorder_agent_recorder_interface_handle_start
    (Hwangsae1DBusRecorderInterface * object,
    GDBusMethodInvocation * invocation, gchar * arg_id, gpointer user_data) {
  g_autofree gchar *cmd = NULL;
  g_autofree gchar *response = NULL;
  gchar *record_id = NULL;
  gint64 rec_id;

  HwangsaeRecorderAgent *self = (HwangsaeRecorderAgent *) user_data;

  g_debug ("hwangsae_recorder_agent_recorder_interface_handle_start, cmd %s",
      cmd);

  rec_id = hwangsae_recorder_agent_start_recording (self, arg_id);

  if (rec_id > 0)
    record_id = g_strdup_printf ("%ld", rec_id);
  else
    record_id = g_strdup ("");

  hwangsae1_dbus_recorder_interface_complete_start (object, invocation,
      record_id);

  return TRUE;
}

gboolean
    hwangsae_recorder_agent_recorder_interface_handle_stop
    (Hwangsae1DBusRecorderInterface * object,
    GDBusMethodInvocation * invocation, gchar * arg_id, gpointer user_data) {
  g_autofree gchar *cmd = NULL;
  g_autofree gchar *response = NULL;

  HwangsaeRecorderAgent *self = (HwangsaeRecorderAgent *) user_data;

  g_debug ("hwangsae_recorder_agent_recorder_interface_handle_stop, cmd %s",
      cmd);

  hwangsae_recorder_agent_stop_recording (self, arg_id);

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
  if (!(parts[0] && parts[1]))
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

  dir = g_dir_open (recording_dir, 0, &error);
  if (error) {
    g_dir_close (dir);
    g_error_free (error);
    return NULL;
  }

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
          g_variant_builder_add (builder, "(sxxx)", g_strdup (file_id),
              file_start, file_end, st.st_size);
        else if (!arg_record_id)
          g_variant_builder_add (builder, "(ssxxx)", g_strdup (record_id),
              g_strdup (file_id), file_start, file_end, st.st_size);
      }
    }
  }

  if (!edge_id)
    edge_id = g_strdup ("");

  return edge_id;
}

gboolean
    hwangsae_recorder_agent_recorder_interface_handle_lookup_by_record
    (Hwangsae1DBusRecorderInterface * object,
    GDBusMethodInvocation * invocation, gchar * arg_record_id, gint64 arg_from,
    gint64 arg_to, gpointer user_data) {
  HwangsaeRecorderAgent *self = (HwangsaeRecorderAgent *) user_data;
  g_autofree gchar *cmd = NULL;
  g_autofree gchar *response = NULL;
  g_autofree gchar *recording_dir;
  g_autofree gchar *edge_id = NULL;
  GVariantBuilder *builder;
  GVariant *records;

  g_debug
      ("hwangsae_recorder_agent_recorder_interface_handle_lookup_by_record");

  recording_dir = g_settings_get_string (self->settings, "recording-dir");

  builder = g_variant_builder_new (G_VARIANT_TYPE ("a(sxxx)"));

  edge_id = get_records (recording_dir, NULL, arg_record_id, arg_from, arg_to,
      builder);

  records = g_variant_new ("a(sxxx)", builder);

  g_variant_builder_unref (builder);

  hwangsae1_dbus_recorder_interface_complete_lookup_by_record (object,
      invocation, edge_id, records);

  return TRUE;
}

gboolean
    hwangsae_recorder_agent_recorder_interface_handle_lookup_by_edge
    (Hwangsae1DBusRecorderInterface * object,
    GDBusMethodInvocation * invocation, gchar * arg_edge_id, gint64 arg_from,
    gint64 arg_to, gpointer user_data) {
  HwangsaeRecorderAgent *self = (HwangsaeRecorderAgent *) user_data;
  g_autofree gchar *cmd = NULL;
  g_autofree gchar *response = NULL;
  g_autofree gchar *recording_dir = NULL;
  g_autofree gchar *edge_id = NULL;
  GVariantBuilder *builder;
  GVariant *records;

  g_debug ("hwangsae_recorder_agent_recorder_interface_handle_lookup_by_edge");

  recording_dir = g_settings_get_string (self->settings, "recording-dir");

  builder = g_variant_builder_new (G_VARIANT_TYPE ("a(ssxxx)"));

  edge_id = get_records (recording_dir, arg_edge_id, NULL, arg_from, arg_to,
      builder);

  records = g_variant_new ("a(ssxxx)", builder);

  g_variant_builder_unref (builder);

  hwangsae1_dbus_recorder_interface_complete_lookup_by_edge (object,
      invocation, records);

  return TRUE;
}

static void
hwangsae_recorder_agent_dispose (GObject * object)
{
  HwangsaeRecorderAgent *self = HWANGSAE_RECORDER_AGENT (object);

  g_clear_object (&self->recorder);
  g_clear_object (&self->settings);
  g_clear_object (&self->manager);
  g_clear_object (&self->recorder_interface);
  g_clear_object (&self->hwangsae_http_server);

  G_OBJECT_CLASS (hwangsae_recorder_agent_parent_class)->dispose (object);
}

static void
hwangsae_recorder_agent_class_init (HwangsaeRecorderAgentClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  gobject_class->dispose = hwangsae_recorder_agent_dispose;

  app_class->dbus_register = hwangsae_recorder_agent_dbus_register;
  app_class->dbus_unregister = hwangsae_recorder_agent_dbus_unregister;
}

static gboolean
signal_handler (GApplication * app)
{
  g_application_release (app);

  return G_SOURCE_REMOVE;
}

static void
hwangsae_recorder_agent_init (HwangsaeRecorderAgent * self)
{
  g_autofree gchar *recording_dir;

  self->is_recording = FALSE;

  self->settings = g_settings_new (HWANGSAE_RECORDER_SCHEMA_ID);

  recording_dir = g_settings_get_string (self->settings, "recording-dir");

  self->recorder = hwangsae_recorder_new ();

  self->hwangsae_http_server =
      hwangsae_http_server_new (g_settings_get_uint (self->settings,
          "http-port"));
  hwangsae_http_server_set_recording_dir (self->hwangsae_http_server,
      recording_dir);

  self->manager = hwangsae1_dbus_manager_skeleton_new ();

  hwangsae1_dbus_manager_set_status (self->manager, 1);

  self->recorder_interface = hwangsae1_dbus_recorder_interface_skeleton_new ();

  g_signal_connect (self->recorder_interface, "handle-start",
      G_CALLBACK (hwangsae_recorder_agent_recorder_interface_handle_start),
      self);

  g_signal_connect (self->recorder_interface, "handle-stop",
      G_CALLBACK (hwangsae_recorder_agent_recorder_interface_handle_stop),
      self);

  g_signal_connect (self->recorder_interface, "handle-lookup-by-record",
      G_CALLBACK
      (hwangsae_recorder_agent_recorder_interface_handle_lookup_by_record),
      self);

  g_signal_connect (self->recorder_interface, "handle-lookup-by-edge",
      G_CALLBACK
      (hwangsae_recorder_agent_recorder_interface_handle_lookup_by_edge), self);

  hwangsae_recorder_set_container (self->recorder, HWANGSAE_CONTAINER_TS);
}

int
main (int argc, char *argv[])
{
  g_autoptr (GApplication) app = NULL;

  app = G_APPLICATION (g_object_new (HWANGSAE_TYPE_RECORDER_AGENT,
          "application-id", "org.hwangsaeul.Hwangsae1.RecorderAgent",
          "flags", G_APPLICATION_IS_SERVICE, NULL));

  g_unix_signal_add (SIGINT, (GSourceFunc) signal_handler, app);

  gst_init (&argc, &argv);

  g_application_hold (app);

  return g_application_run (app, argc, argv);
}
