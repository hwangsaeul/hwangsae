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
} HwangsaeRecorderPrivate;

/* *INDENT-OFF* */
G_DEFINE_TYPE_WITH_PRIVATE (HwangsaeRecorder, hwangsae_recorder, G_TYPE_OBJECT)
/* *INDENT-ON* */

HwangsaeRecorder *
hwangsae_recorder_new (void)
{
  return g_object_new (HWANGSAE_TYPE_RECORDER, NULL);
}

void
hwangsae_recorder_start_recording (HwangsaeRecorder * self, const gchar * uri)
{
  HwangsaeRecorderPrivate *priv = hwangsae_recorder_get_instance_private (self);

  g_autofree gchar *pipeline_str = NULL;
  g_autoptr (GError) error = NULL;

  g_return_if_fail (!priv->pipeline);

  pipeline_str =
      g_strdup_printf
      ("urisourcebin uri=%s ! tsdemux ! h264parse ! mp4mux ! filesink location=/tmp/hwangsae-recording-%ld.mp4",
      uri, g_get_real_time ());

  priv->pipeline = gst_parse_launch (pipeline_str, &error);
  gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
}

void
hwangsae_recorder_stop_recording (HwangsaeRecorder * self)
{
  HwangsaeRecorderPrivate *priv = hwangsae_recorder_get_instance_private (self);

  g_return_if_fail (priv->pipeline);

  // TODO: flush the pipeline contents, etc.
  gst_element_set_state (priv->pipeline, GST_STATE_NULL);
  gst_clear_object (&priv->pipeline);
}

static void
hwangsae_recorder_class_init (HwangsaeRecorderClass * klass)
{
}

static void
hwangsae_recorder_init (HwangsaeRecorder * self)
{
}
