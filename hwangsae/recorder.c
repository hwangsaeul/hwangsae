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

#include "recorder.h"

#include <gst/gst.h>

struct _HwangsaeRecorder
{
  GObject parent;
};

typedef struct
{
  GstElement *pipeline;

  gchar *recording_path;
} HwangsaeRecorderPrivate;

/* *INDENT-OFF* */
G_DEFINE_TYPE_WITH_PRIVATE (HwangsaeRecorder, hwangsae_recorder, G_TYPE_OBJECT)
/* *INDENT-ON* */

enum
{
  FILE_CREATED_SIGNAL,
  FILE_COMPLETED_SIGNAL,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

HwangsaeRecorder *
hwangsae_recorder_new (void)
{
  return g_object_new (HWANGSAE_TYPE_RECORDER, NULL);
}

static void
hwangsae_recorder_stop_recording_internal (HwangsaeRecorder * self)
{
  HwangsaeRecorderPrivate *priv = hwangsae_recorder_get_instance_private (self);

  gst_element_set_state (priv->pipeline, GST_STATE_NULL);
  gst_clear_object (&priv->pipeline);

  g_signal_emit (self, signals[FILE_COMPLETED_SIGNAL], 0, priv->recording_path);
  g_clear_pointer (&priv->recording_path, g_free);

  g_debug ("Recording stopped");
}

static gboolean
gst_bus_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  if (message->type == GST_MESSAGE_EOS) {
    hwangsae_recorder_stop_recording_internal (HWANGSAE_RECORDER (data));
  } else {
    g_debug ("Got Gst message %s", GST_MESSAGE_TYPE_NAME (message));
  }

  return TRUE;
}

void
hwangsae_recorder_start_recording (HwangsaeRecorder * self, const gchar * uri)
{
  HwangsaeRecorderPrivate *priv = hwangsae_recorder_get_instance_private (self);

  g_autoptr (GstBus) bus = NULL;
  g_autofree gchar *pipeline_str = NULL;
  g_autoptr (GError) error = NULL;

  g_return_if_fail (!priv->pipeline);

  priv->recording_path =
      g_strdup_printf ("/tmp/hwangsae-recording-%ld.mp4", g_get_real_time ());

  pipeline_str =
      g_strdup_printf
      ("urisourcebin uri=%s name=srcbin ! tsdemux ! h264parse ! mp4mux ! filesink location=%s",
      uri, priv->recording_path);

  priv->pipeline = gst_parse_launch (pipeline_str, &error);

  bus = gst_element_get_bus (priv->pipeline);
  gst_bus_add_watch (bus, gst_bus_cb, self);

  gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);

  g_signal_emit (self, signals[FILE_CREATED_SIGNAL], 0, priv->recording_path);
}

void
hwangsae_recorder_stop_recording (HwangsaeRecorder * self)
{
  HwangsaeRecorderPrivate *priv = hwangsae_recorder_get_instance_private (self);

  g_autoptr (GstElement) srcbin = NULL;

  g_return_if_fail (priv->pipeline);

  srcbin = gst_bin_get_by_name (GST_BIN (priv->pipeline), "srcbin");

  gst_element_send_event (srcbin, gst_event_new_eos ());
}

static void
hwangsae_recorder_class_init (HwangsaeRecorderClass * klass)
{
  signals[FILE_CREATED_SIGNAL] =
      g_signal_new ("file-created", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

  signals[FILE_COMPLETED_SIGNAL] =
      g_signal_new ("file-completed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
hwangsae_recorder_init (HwangsaeRecorder * self)
{
}
