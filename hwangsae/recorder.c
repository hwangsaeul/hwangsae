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

#include "enumtypes.h"

#include <gio/gio.h>
#include <gst/gst.h>

/* *INDENT-OFF* */
#if !GLIB_CHECK_VERSION(2,57,1)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GEnumClass, g_type_class_unref)
#endif
/* *INDENT-ON* */

struct _HwangsaeRecorder
{
  GObject parent;
};

typedef struct
{
  GSettings *settings;
  GstElement *pipeline;

  gchar *recording_dir;
  gint64 record_id;
  HwangsaeContainer container;
  guint64 max_size_time;
  guint64 max_size_bytes;
} HwangsaeRecorderPrivate;

/* *INDENT-OFF* */
G_DEFINE_TYPE_WITH_PRIVATE (HwangsaeRecorder, hwangsae_recorder, G_TYPE_OBJECT)
/* *INDENT-ON* */

enum
{
  PROP_RECORDING_DIR = 1,
  PROP_CONTAINER,
  PROP_MAX_SIZE_TIME,
  PROP_MAX_SIZE_BYTES,
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

void
hwangsae_recorder_set_container (HwangsaeRecorder * self,
    HwangsaeContainer container)
{
  g_object_set (self, "container", container, NULL);
}

HwangsaeContainer
hwangsae_recorder_get_container (HwangsaeRecorder * self)
{
  HwangsaeContainer result;

  g_object_get (self, "container", &result, NULL);

  return result;
}

void
hwangsae_recorder_set_max_size_time (HwangsaeRecorder * self,
    guint64 duration_ns)
{
  g_object_set (self, "max-size-time", duration_ns, NULL);
}

guint64
hwangsae_recorder_get_max_size_time (HwangsaeRecorder * self)
{
  guint64 result;

  g_object_get (self, "max-size-time", &result, NULL);

  return result;
}

void
hwangsae_recorder_set_max_size_bytes (HwangsaeRecorder * self, guint64 size)
{
  g_object_set (self, "max-size-bytes", size, NULL);
}

guint64
hwangsae_recorder_get_max_size_bytes (HwangsaeRecorder * self)
{
  guint64 result;

  g_object_get (self, "max-size-bytes", &result, NULL);

  return result;
}

static void
hwangsae_recorder_stop_recording_internal (HwangsaeRecorder * self)
{
  HwangsaeRecorderPrivate *priv = hwangsae_recorder_get_instance_private (self);

  priv->record_id = -ENOENT;
  gst_element_set_state (priv->pipeline, GST_STATE_NULL);
  g_clear_pointer (&priv->pipeline, gst_object_unref);

  g_signal_emit (self, signals[STREAM_DISCONNECTED_SIGNAL], 0);

  g_debug ("Recording stopped");
}

static gboolean
gst_bus_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  HwangsaeRecorder *recorder = HWANGSAE_RECORDER (data);

  switch (message->type) {
    case GST_MESSAGE_APPLICATION:{
      const gchar *name =
          gst_structure_get_name (gst_message_get_structure (message));

      if (g_str_equal (name, "hwangsae-recorder-first-frame")) {
        g_signal_emit (recorder, signals[STREAM_CONNECTED_SIGNAL], 0);
      }
      break;
    }
    case GST_MESSAGE_ELEMENT:{
      const GstStructure *s = gst_message_get_structure (message);

      if (gst_structure_has_name (s, "splitmuxsink-fragment-opened")) {
        g_signal_emit (recorder, signals[FILE_CREATED_SIGNAL], 0,
            gst_structure_get_string (s, "location"));
      } else if (gst_structure_has_name (s, "splitmuxsink-fragment-closed")) {
        g_signal_emit (recorder, signals[FILE_COMPLETED_SIGNAL], 0,
            gst_structure_get_string (s, "location"));
      }
      break;
    }
    case GST_MESSAGE_EOS:
      hwangsae_recorder_stop_recording_internal (recorder);
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


gint64
hwangsae_recorder_get_recording_id (HwangsaeRecorder * self)
{
  HwangsaeRecorderPrivate *priv = hwangsae_recorder_get_instance_private (self);

  return priv->record_id;
}

gint64
hwangsae_recorder_start_recording (HwangsaeRecorder * self, const gchar * uri)
{
  HwangsaeRecorderPrivate *priv = hwangsae_recorder_get_instance_private (self);

  g_autoptr (GEnumClass) enum_class = NULL;
  GEnumValue *container;
  g_autoptr (GstBus) bus = NULL;
  g_autoptr (GstElement) element = NULL;
  g_autoptr (GstPad) parse_src = NULL;
  g_autofree gchar *recording_file = NULL;
  g_autofree gchar *pipeline_str = NULL;
  const gchar *mux_name;
  g_autoptr (GError) error = NULL;

  g_return_val_if_fail (!priv->pipeline, ENOENT);

  g_mkdir_with_parents (priv->recording_dir, 0750);

  enum_class = g_type_class_ref (HWANGSAE_TYPE_CONTAINER);
  container = g_enum_get_value (enum_class, priv->container);

  priv->record_id = g_get_real_time ();

  g_debug ("record_id %ld", priv->record_id);

  recording_file = g_build_filename (priv->recording_dir,
      "hwangsae-recording-%ld-%%05d.%s", NULL);
  recording_file = g_strdup_printf (recording_file, priv->record_id,
      container->value_nick);

  switch (priv->container) {
    case HWANGSAE_CONTAINER_MP4:
      mux_name = "mp4mux";
      break;
    case HWANGSAE_CONTAINER_TS:
      mux_name = "mpegtsmux";
      break;
    default:
      g_error ("Unknown container format %s", container->value_nick);
  }

  pipeline_str =
      g_strdup_printf
      ("urisourcebin uri=%s name=srcbin ! tsdemux ! h264parse name=parse ! "
      "splitmuxsink name=sink async-finalize=true muxer-factory=%s",
      uri, mux_name);

  priv->pipeline = gst_parse_launch (pipeline_str, &error);

  bus = gst_element_get_bus (priv->pipeline);
  gst_bus_add_watch (bus, gst_bus_cb, self);

  element = gst_bin_get_by_name (GST_BIN (priv->pipeline), "parse");
  parse_src = gst_element_get_static_pad (element, "src");
  gst_pad_add_probe (parse_src,
      GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST,
      first_buffer_cb, bus, NULL);

  element = gst_bin_get_by_name (GST_BIN (priv->pipeline), "sink");
  g_object_set (element,
      "location", recording_file,
      "max-size-time", priv->max_size_time,
      "max-size-bytes", priv->max_size_bytes, NULL);

  gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);

  return priv->record_id;
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
    case PROP_CONTAINER:
      priv->container = g_value_get_enum (value);
      break;
    case PROP_MAX_SIZE_TIME:
      priv->max_size_time = g_value_get_uint64 (value);
      break;
    case PROP_MAX_SIZE_BYTES:
      priv->max_size_bytes = g_value_get_uint64 (value);
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
    case PROP_CONTAINER:
      g_value_set_enum (value, priv->container);
      break;
    case PROP_MAX_SIZE_TIME:
      g_value_set_uint64 (value, priv->max_size_time);
      break;
    case PROP_MAX_SIZE_BYTES:
      g_value_set_uint64 (value, priv->max_size_bytes);
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

  g_object_class_install_property (gobject_class, PROP_CONTAINER,
      g_param_spec_enum ("container", "Media container",
          "Media container format of the recording file",
          HWANGSAE_TYPE_CONTAINER, HWANGSAE_CONTAINER_MP4,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_TIME,
      g_param_spec_uint64 ("max-size-time", "Max recording file duration",
          "Max amount of time per recording file (in ns, 0 = disable)",
          0, G_MAXUINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_BYTES,
      g_param_spec_uint64 ("max-size-bytes", "Max recording file size in bytes",
          "Max amount of bytes per recording file (0 = disable)",
          0, G_MAXUINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
  g_autofree gchar *dir = NULL;

  priv->settings = g_settings_new ("org.hwangsaeul.hwangsae.recorder");

  priv->record_id = -ENOENT;

  dir = g_build_filename (g_get_user_data_dir (),
      "hwangsaeul", "hwangsae", "recordings", NULL);

  g_object_set (self, "recording-dir", dir, NULL);
}
