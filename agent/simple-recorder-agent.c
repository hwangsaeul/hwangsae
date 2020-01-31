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

#include "simple-recorder-agent.h"
#include <hwangsae/recorder.h>
#include <glib/gstdio.h>
#include <gst/gst.h>

#include <glib-unix.h>

struct _HwangsaeSimpleRecorderAgent
{
  HwangsaeRecorderAgent parent;

  HwangsaeRecorder *recorder;
  gint64 recording_id;
  gboolean is_recording;
  gchar *edge_id;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (HwangsaeSimpleRecorderAgent, hwangsae_simple_recorder_agent, HWANGSAE_TYPE_RECORDER_AGENT)
/* *INDENT-ON* */

static gint64
hwangsae_simple_recorder_agent_start_recording (HwangsaeRecorderAgent *
    recorder_agent, gchar * edge_id)
{
  HwangsaeSimpleRecorderAgent *self =
      HWANGSAE_SIMPLE_RECORDER_AGENT (recorder_agent);
  g_autoptr (GError) error = NULL;
  g_autofree gchar *recorder_id =
      hwangsae_recorder_agent_get_recorder_id (recorder_agent);
  g_autofree gchar *host =
      hwangsae_recorder_agent_get_relay_address (recorder_agent);
  guint port = hwangsae_recorder_agent_get_relay_stream_port (recorder_agent);
  g_autofree gchar *recording_dir =
      hwangsae_recorder_agent_get_recording_dir (recorder_agent);
  g_autofree gchar *streamid_tmp = NULL;
  g_autofree gchar *streamid = NULL;
  g_autofree gchar *url = NULL;
  g_autofree gchar *recording_edge_dir = NULL;
  g_autofree gchar *filename_prefix = NULL;

  if (self->is_recording) {
    g_warning ("recording already started");
    return self->recording_id;
  }

  self->recording_id = g_get_real_time ();
  self->is_recording = TRUE;
  g_clear_pointer (&self->edge_id, g_free);
  self->edge_id = g_strdup (edge_id);

  hwangsae_recorder_agent_send_rest_api (recorder_agent,
      RELAY_METHOD_START_STREAMING, edge_id);

  streamid_tmp = g_strdup_printf ("#!::r=%s,u=%s", edge_id, recorder_id);
  streamid = g_uri_escape_string (streamid_tmp, NULL, FALSE);
  url = g_strdup_printf ("srt://%s:%d?streamid=%s", host, port, streamid);

  g_debug ("starting to recording stream from %s", url);

  recording_edge_dir = g_build_filename (recording_dir, edge_id, NULL);

  filename_prefix = g_strdup_printf ("hwangsae-recording-%ld",
      self->recording_id);

  g_debug ("setting recording_dir: %s, filename_prefix: %s\n",
      recording_edge_dir, filename_prefix);

  hwangsae_recorder_set_recording_dir (self->recorder, recording_edge_dir);

  hwangsae_recorder_set_filename_prefix (self->recorder, filename_prefix);

  hwangsae_recorder_set_container (self->recorder, HWANGSAE_CONTAINER_TS);

  hwangsae_recorder_start_recording (self->recorder, url);

  return self->recording_id;
}

static void
hwangsae_simple_recorder_agent_stop_recording (HwangsaeRecorderAgent *
    recorder_agent, gchar * edge_id)
{
  g_autoptr (GError) error = NULL;
  HwangsaeSimpleRecorderAgent *self =
      HWANGSAE_SIMPLE_RECORDER_AGENT (recorder_agent);

  if (!self->is_recording) {
    g_warning ("recording already stopped");
    return;
  }

  if (g_strcmp0 (self->edge_id, edge_id)) {
    g_warning ("edge_id mismatch");
    return;
  }

  self->is_recording = FALSE;
  g_clear_pointer (&self->edge_id, g_free);
  self->recording_id = 0;

  hwangsae_recorder_stop_recording (self->recorder);
}

static void
hwangsae_simple_recorder_agent_init (HwangsaeSimpleRecorderAgent * self)
{
  self->recorder = hwangsae_recorder_new ();
  self->is_recording = FALSE;
  self->edge_id = NULL;
}

static gboolean
signal_handler (GApplication * app)
{
  g_application_release (app);

  return G_SOURCE_REMOVE;
}

static void
hwangsae_simple_recorder_agent_dispose (GObject * object)
{
  HwangsaeSimpleRecorderAgent *self = HWANGSAE_SIMPLE_RECORDER_AGENT (object);

  g_clear_object (&self->recorder);
  g_clear_pointer (&self->edge_id, g_free);

  G_OBJECT_CLASS (hwangsae_simple_recorder_agent_parent_class)->dispose
      (object);
}

static void
hwangsae_simple_recorder_agent_class_init (HwangsaeSimpleRecorderAgentClass *
    klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  HwangsaeRecorderAgentClass *recorder_agent_class =
      HWANGSAE_RECORDER_AGENT_CLASS (klass);

  gobject_class->dispose = hwangsae_simple_recorder_agent_dispose;

  recorder_agent_class->start_recording =
      hwangsae_simple_recorder_agent_start_recording;
  recorder_agent_class->stop_recording =
      hwangsae_simple_recorder_agent_stop_recording;

}

int
main (int argc, char *argv[])
{
  g_autoptr (GApplication) app = NULL;

  app = G_APPLICATION (g_object_new (HWANGSAE_TYPE_SIMPLE_RECORDER_AGENT,
          "application-id", "org.hwangsaeul.Hwangsae1.RecorderAgent",
          "flags", G_APPLICATION_IS_SERVICE, NULL));

  g_unix_signal_add (SIGINT, (GSourceFunc) signal_handler, app);

  gst_init (&argc, &argv);

  g_application_hold (app);

  return hwangsaeul_application_run (HWANGSAEUL_APPLICATION (app), argc, argv);
}
