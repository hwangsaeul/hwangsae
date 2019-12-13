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

#include "multi-recorder-agent.h"
#include <hwangsae/recorder.h>
#include <gst/gst.h>

#include <glib-unix.h>

struct _HwangsaeMultiRecorderAgent
{
  HwangsaeRecorderAgent parent;

  GHashTable *edge_map;
  GMutex lock;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (HwangsaeMultiRecorderAgent, hwangsae_multi_recorder_agent, HWANGSAE_TYPE_RECORDER_AGENT)
/* *INDENT-ON* */

#define LOCK_MULTI_RECORDER_AGENT \
  g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&self->lock)

typedef struct
{
  gint64 recording_id;
  HwangsaeRecorder *recorder;
} RecordingData;

static void
_recording_data_free (RecordingData * recording_data)
{
  g_object_unref (recording_data->recorder);
  g_free (recording_data);
}

static void
stream_disconnected_cb (HwangsaeRecorder * recorder,
    HwangsaeRecorderAgent * self)
{
  g_debug ("Stream disconnected, cleaning recorder");

  g_clear_object (&recorder);
}

static gint64
hwangsae_multi_recorder_agent_start_recording (HwangsaeRecorderAgent *
    recorder_agent, gchar * edge_id)
{
  HwangsaeMultiRecorderAgent *self =
      HWANGSAE_MULTI_RECORDER_AGENT (recorder_agent);
  g_autoptr (GError) error = NULL;
  g_autofree gchar *host =
      hwangsae_recorder_agent_get_relay_address (recorder_agent);
  g_autofree gchar *recorder_id =
      hwangsae_recorder_agent_get_recorder_id (recorder_agent);
  guint port = hwangsae_recorder_agent_get_relay_stream_port (recorder_agent);
  g_autofree gchar *recording_dir =
      hwangsae_recorder_agent_get_recording_dir (recorder_agent);
  g_autofree gchar *streamid_tmp = NULL;
  g_autofree gchar *streamid = NULL;
  g_autofree gchar *url = NULL;
  g_autofree gchar *recording_edge_dir = NULL;
  g_autofree gchar *filename_prefix = NULL;
  RecordingData *recording_data;
  gchar *edge_id_key;

  LOCK_MULTI_RECORDER_AGENT;

  recording_data = g_hash_table_lookup (self->edge_map, edge_id);
  if (recording_data) {
    g_warning ("recording already started");
    return recording_data->recording_id;
  }

  recording_data = g_new0 (RecordingData, 1);
  recording_data->recording_id = g_get_real_time ();
  recording_data->recorder = hwangsae_recorder_new ();

  hwangsae_recorder_agent_send_rest_api (recorder_agent,
      RELAY_METHOD_START_STREAMING, edge_id);

  streamid_tmp = g_strdup_printf ("#!::r=%s,u=%s", edge_id, recorder_id);
  streamid = g_uri_escape_string (streamid_tmp, NULL, FALSE);
  url = g_strdup_printf ("srt://%s:%d?streamid=%s", host, port, streamid);

  g_debug ("starting to recording stream from %s", url);

  if (g_str_equal (recording_dir, "")) {
    g_free (recording_dir);
    recording_dir = g_build_filename (g_get_user_data_dir (),
        "hwangsaeul", "hwangsae", "recordings", NULL);
  }

  recording_edge_dir = g_build_filename (recording_dir, edge_id, NULL);

  filename_prefix = g_strdup_printf ("hwangsae-recording-%ld",
      recording_data->recording_id);

  g_debug ("setting recording_dir: %s, filename_prefix: %s\n",
      recording_edge_dir, filename_prefix);

  hwangsae_recorder_set_recording_dir (recording_data->recorder,
      recording_edge_dir);

  hwangsae_recorder_set_filename_prefix (recording_data->recorder,
      filename_prefix);

  hwangsae_recorder_set_container (recording_data->recorder,
      HWANGSAE_CONTAINER_TS);

  g_signal_connect (recording_data->recorder, "stream-disconnected",
      (GCallback) stream_disconnected_cb, self);

  edge_id_key = g_strdup (edge_id);
  g_hash_table_insert (self->edge_map, edge_id_key, recording_data);

  hwangsae_recorder_start_recording (recording_data->recorder, url);

  return recording_data->recording_id;
}

static void
hwangsae_multi_recorder_agent_stop_recording (HwangsaeRecorderAgent *
    recorder_agent, gchar * edge_id)
{
  g_autoptr (GError) error = NULL;
  RecordingData *recording_data;
  HwangsaeMultiRecorderAgent *self =
      HWANGSAE_MULTI_RECORDER_AGENT (recorder_agent);

  LOCK_MULTI_RECORDER_AGENT;

  recording_data = g_hash_table_lookup (self->edge_map, edge_id);

  if (!recording_data) {
    g_warning ("recording already stopped");
    return;
  }

  /* Will get released in stream_disconnected_cb. */
  g_object_ref (recording_data->recorder);

  hwangsae_recorder_stop_recording (recording_data->recorder);

  g_hash_table_remove (self->edge_map, edge_id);
}

static void
hwangsae_multi_recorder_agent_init (HwangsaeMultiRecorderAgent * self)
{

  self->edge_map =
      g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free,
      (GDestroyNotify) _recording_data_free);
}

static gboolean
signal_handler (GApplication * app)
{
  g_application_release (app);

  return G_SOURCE_REMOVE;
}

static void
hwangsae_multi_recorder_agent_dispose (GObject * object)
{
  HwangsaeMultiRecorderAgent *self = HWANGSAE_MULTI_RECORDER_AGENT (object);

  g_hash_table_destroy (self->edge_map);

  G_OBJECT_CLASS (hwangsae_multi_recorder_agent_parent_class)->dispose (object);
}

static void
hwangsae_multi_recorder_agent_class_init (HwangsaeMultiRecorderAgentClass *
    klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  HwangsaeRecorderAgentClass *recorder_agent_class =
      HWANGSAE_RECORDER_AGENT_CLASS (klass);

  gobject_class->dispose = hwangsae_multi_recorder_agent_dispose;

  recorder_agent_class->start_recording =
      hwangsae_multi_recorder_agent_start_recording;
  recorder_agent_class->stop_recording =
      hwangsae_multi_recorder_agent_stop_recording;

}

int
main (int argc, char *argv[])
{
  g_autoptr (GApplication) app = NULL;

  app = G_APPLICATION (g_object_new (HWANGSAE_TYPE_MULTI_RECORDER_AGENT,
          "application-id", "org.hwangsaeul.Hwangsae1.RecorderAgent",
          "flags", G_APPLICATION_IS_SERVICE, NULL));

  g_unix_signal_add (SIGINT, (GSourceFunc) signal_handler, app);

  gst_init (&argc, &argv);

  g_application_hold (app);

  return hwangsaeul_application_run (HWANGSAEUL_APPLICATION (app), argc, argv);
}
