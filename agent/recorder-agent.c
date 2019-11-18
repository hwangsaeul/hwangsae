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
#include <hwangsae/recorder.h>
#include <gst/gst.h>
#include <curl/curl.h>

#include <glib-unix.h>

#include <hwangsae/dbus/manager-generated.h>
#include <hwangsae/dbus/recorder-interface-generated.h>

#define RECORDER_GAEGULI_SRT_MODE GAEGULI_SRT_MODE_CALLER

struct _HwangsaeRecorderAgent
{
  GApplication parent;

  HwangsaeRecorder *recorder;
  Hwangsae1DBusManager *manager;
  Hwangsae1DBusRecorderInterface *recorder_interface;

  gboolean is_recording;
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
hwangsae_recorder_agent_send_rest_api (relay_methods method, gchar * edge_id)
{
  CURL *curl;
  g_autofree gchar *url = NULL;
  g_autofree gchar *postData = NULL;

  struct curl_slist *hs = NULL;
  hs = curl_slist_append (hs, "Content-Type: application/json");

  if (method == RELAY_METHOD_START_STREAMING) {
    url =
        g_strdup_printf ("http://localhost:8080/api/v1.0/srt/%s/%s", "start",
        edge_id);
    postData = g_strdup ("{}");
  } else if (method == RELAY_METHOD_STOP_STREAMING) {
    url =
        g_strdup_printf ("http://localhost:8080/api/v1.0/srt/%s/%s", "stop",
        edge_id);
    postData = g_strdup ("{}");
  }

  curl = curl_easy_init ();
  if (curl) {
    curl_easy_setopt (curl, CURLOPT_URL, url);
    curl_easy_setopt (curl, CURLOPT_HTTPHEADER, hs);
    if (postData)
      curl_easy_setopt (curl, CURLOPT_POSTFIELDS, postData);
    curl_easy_perform (curl);
    curl_easy_cleanup (curl);
  }
  return;
}

static gint64
hwangsae_recorder_agent_start_recording (HwangsaeRecorderAgent * self,
    gchar * edge_id)
{
  g_autoptr (GError) error = NULL;


  if (self->is_recording) {
    g_warning ("recording already started");
    return hwangsae_recorder_get_recording_id (self->recorder);
  }

  self->is_recording = TRUE;

  hwangsae_recorder_agent_send_rest_api (RELAY_METHOD_START_STREAMING, edge_id);

#if RECORDER_GAEGULI_SRT_MODE == GAEGULI_SRT_MODE_LISTENER
  return hwangsae_recorder_start_recording (self->recorder,
      "srt://192.168.1.3:8888?mode=listener");
#else
  return hwangsae_recorder_start_recording (self->recorder,
      "srt://192.168.1.3:8888?mode=caller");
#endif
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

  hwangsae_recorder_agent_send_rest_api (RELAY_METHOD_STOP_STREAMING, edge_id);

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

static void
hwangsae_recorder_agent_dispose (GObject * object)
{
  HwangsaeRecorderAgent *self = HWANGSAE_RECORDER_AGENT (object);

  g_clear_object (&self->recorder);
  g_clear_object (&self->manager);
  g_clear_object (&self->recorder_interface);

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
  self->is_recording = FALSE;

  self->recorder = hwangsae_recorder_new ();

  self->manager = hwangsae1_dbus_manager_skeleton_new ();

  hwangsae1_dbus_manager_set_status (self->manager, 1);

  self->recorder_interface = hwangsae1_dbus_recorder_interface_skeleton_new ();

  g_signal_connect (self->recorder_interface, "handle-start",
      G_CALLBACK (hwangsae_recorder_agent_recorder_interface_handle_start),
      self);

  g_signal_connect (self->recorder_interface, "handle-stop",
      G_CALLBACK (hwangsae_recorder_agent_recorder_interface_handle_stop),
      self);

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
