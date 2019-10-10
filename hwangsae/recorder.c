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

#include <gio/gio.h>
#include <gst/gst.h>

struct _HwangsaeRecorder
{
  GObject parent;
};

typedef struct
{
  GSettings *settings;
  GstElement *pipeline;

  gchar *recording_file;

  gchar *recording_dir;
} HwangsaeRecorderPrivate;

/* *INDENT-OFF* */
G_DEFINE_TYPE_WITH_PRIVATE (HwangsaeRecorder, hwangsae_recorder, G_TYPE_OBJECT)
/* *INDENT-ON* */

enum
{
  PROP_RECORDING_DIR = 1,
  PROP_LAST
};

enum
{
  STREAM_CONNECTED_SIGNAL,
  STREAM_DISCONNECTED_SIGNAL,
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
  g_clear_pointer (&priv->pipeline, gst_object_unref);

  g_signal_emit (self, signals[FILE_COMPLETED_SIGNAL], 0, priv->recording_file);
  g_clear_pointer (&priv->recording_file, g_free);

  g_signal_emit (self, signals[STREAM_DISCONNECTED_SIGNAL], 0);

  g_debug ("Recording stopped");
}

static void
hwangsae_recorder_emit_stream_connected (HwangsaeRecorder * self)
{
  g_signal_emit (self, signals[STREAM_CONNECTED_SIGNAL], 0);
}

static gboolean
gst_bus_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  switch (message->type) {
    case GST_MESSAGE_APPLICATION:{
      const gchar *name =
          gst_structure_get_name (gst_message_get_structure (message));

      if (g_str_equal (name, "hwangsae-recorder-first-frame")) {
        hwangsae_recorder_emit_stream_connected (HWANGSAE_RECORDER (data));
      }
      break;
    }
    case GST_MESSAGE_EOS:
      hwangsae_recorder_stop_recording_internal (HWANGSAE_RECORDER (data));
      break;
    default:
      break;
  }

  g_debug ("Got Gst message %s from %s", GST_MESSAGE_TYPE_NAME (message),
      GST_MESSAGE_SRC_NAME (message));

  return TRUE;
}

static GstPadProbeReturn
first_buffer_cb (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  gst_bus_post (GST_BUS (data),
      gst_message_new_application (NULL,
          gst_structure_new_empty ("hwangsae-recorder-first-frame")));

  return GST_PAD_PROBE_REMOVE;
}

void
hwangsae_recorder_start_recording (HwangsaeRecorder * self, const gchar * uri)
{
  HwangsaeRecorderPrivate *priv = hwangsae_recorder_get_instance_private (self);

  g_autoptr (GstBus) bus = NULL;
  g_autoptr (GstElement) parse = NULL;
  g_autoptr (GstPad) parse_src = NULL;
  g_autofree gchar *recording_file = NULL;
  g_autofree gchar *pipeline_str = NULL;
  g_autoptr (GError) error = NULL;

  g_return_if_fail (!priv->pipeline);

  g_mkdir_with_parents (priv->recording_dir, 0750);

  recording_file = g_build_filename (priv->recording_dir,
      "hwangsae-recording-%ld.mp4", NULL);
  recording_file = g_strdup_printf (recording_file, g_get_real_time ());
  priv->recording_file = g_steal_pointer (&recording_file);

  pipeline_str =
      g_strdup_printf
      ("urisourcebin uri=%s name=srcbin ! tsdemux ! h264parse name=parse ! mp4mux ! filesink location=%s",
      uri, priv->recording_file);

  priv->pipeline = gst_parse_launch (pipeline_str, &error);

  bus = gst_element_get_bus (priv->pipeline);
  gst_bus_add_watch (bus, gst_bus_cb, self);

  parse = gst_bin_get_by_name (GST_BIN (priv->pipeline), "parse");
  parse_src = gst_element_get_static_pad (parse, "src");
  gst_pad_add_probe (parse_src,
      GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST,
      first_buffer_cb, bus, NULL);

  gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);

  g_signal_emit (self, signals[FILE_CREATED_SIGNAL], 0, priv->recording_file);
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
hwangsae_recorder_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  HwangsaeRecorderPrivate *priv =
      hwangsae_recorder_get_instance_private (HWANGSAE_RECORDER (object));

  switch (property_id) {
    case PROP_RECORDING_DIR:
      g_clear_pointer (&priv->recording_dir, g_free);
      priv->recording_dir = g_strdup (g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
hwangsae_recorder_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  HwangsaeRecorderPrivate *priv =
      hwangsae_recorder_get_instance_private (HWANGSAE_RECORDER (object));

  switch (property_id) {
    case PROP_RECORDING_DIR:
      g_value_set_string (value, priv->recording_dir);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
hwangsae_recorder_class_init (HwangsaeRecorderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = hwangsae_recorder_set_property;
  gobject_class->get_property = hwangsae_recorder_get_property;

  g_object_class_install_property (gobject_class, PROP_RECORDING_DIR,
      g_param_spec_string ("recording-dir", "Recording directory",
          "Recording directory", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  signals[STREAM_CONNECTED_SIGNAL] =
      g_signal_new ("stream-connected", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  signals[STREAM_DISCONNECTED_SIGNAL] =
      g_signal_new ("stream-disconnected", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

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
  HwangsaeRecorderPrivate *priv = hwangsae_recorder_get_instance_private (self);

  priv->settings = g_settings_new ("org.hwangsaeul.hwangsae");

  g_settings_bind (priv->settings, "recording-dir", self, "recording-dir",
      G_SETTINGS_BIND_DEFAULT);

  if (g_str_equal (priv->recording_dir, "")) {
    g_autofree gchar *dir = g_build_filename (g_get_user_data_dir (),
        "hwangsaeul", "hwangsae", "recordings", NULL);

    g_object_set (self, "recording-dir", dir, NULL);
  }
}
