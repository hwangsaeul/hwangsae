/** 
 *  tests/common
 *
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

#include "test-streamer.h"

#include <gst/gsturi.h>

/* *INDENT-OFF* */
G_DEFINE_TYPE (HwangsaeTestStreamer, hwangsae_test_streamer, G_TYPE_OBJECT)
/* *INDENT-ON* */

enum
{
  PROP_RESOLUTION = 1,
  PROP_USERNAME,
  PROP_LAST
};

static gboolean
hwangsae_test_streamer_thread_func (HwangsaeTestStreamer * self)
{
  g_autoptr (GMainContext) context = g_main_context_new ();
  g_autoptr (GError) error = NULL;
  g_autoptr (GstUri) uri = NULL;
  g_autoptr (GVariant) attributes = NULL;
  GVariantDict attr;

  const gchar *mode_str;
  GaeguliSRTMode mode = GAEGULI_SRT_MODE_UNKNOWN;
  GaeguliTarget *target;

  g_main_context_push_thread_default (context);

  uri = gst_uri_from_string (self->uri);

  mode_str = gst_uri_get_query_value (uri, "mode");
  if (!mode_str || g_str_equal (mode_str, "caller")) {
    mode = GAEGULI_SRT_MODE_CALLER;
  } else if (g_str_equal (mode_str, "listener")) {
    mode = GAEGULI_SRT_MODE_LISTENER;
  }
  g_assert (mode != GAEGULI_SRT_MODE_UNKNOWN);

  g_variant_dict_init (&attr, NULL);

  g_variant_dict_insert (&attr, "codec", "i", GAEGULI_VIDEO_CODEC_H264_X264);
  g_variant_dict_insert (&attr, "is-record", "b", FALSE);
  g_variant_dict_insert (&attr, "uri", "s", self->uri);
  g_variant_dict_insert (&attr, "username", "s", self->username);
  g_variant_dict_insert (&attr, "bitrate", "u", 2048000);

  attributes = g_variant_dict_end (&attr);

  target = gaeguli_pipeline_add_target_full (self->pipeline,
      attributes, &error);
  g_assert_no_error (error);

  gaeguli_target_start (target, &error);
  g_assert_no_error (error);

  while (self->should_stream) {
    g_main_context_iteration (context, FALSE);
  }

  gaeguli_pipeline_remove_target (self->pipeline, target, &error);
  g_assert_no_error (error);

  return TRUE;
}

void
hwangsae_test_streamer_set_uri (HwangsaeTestStreamer * self, const gchar * uri)
{
  g_clear_pointer (&self->uri, g_free);
  self->uri = g_strdup (uri);
}

void
hwangsae_test_streamer_start (HwangsaeTestStreamer * self)
{
  g_assert_null (self->streaming_thread);

  if (!self->pipeline) {
    self->pipeline =
        gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC, NULL,
        self->resolution, 30);

    g_object_set (self->pipeline, "clock-overlay", TRUE, NULL);
  }

  self->should_stream = TRUE;
  self->streaming_thread = g_thread_new ("streaming_thread_func",
      (GThreadFunc) hwangsae_test_streamer_thread_func, self);
}

void
hwangsae_test_streamer_pause (HwangsaeTestStreamer * self)
{
  self->should_stream = FALSE;
  g_clear_pointer (&self->streaming_thread, g_thread_join);
}

void
hwangsae_test_streamer_stop (HwangsaeTestStreamer * self)
{
  hwangsae_test_streamer_pause (self);
  gaeguli_pipeline_stop (self->pipeline);
}

static void
hwangsae_test_streamer_init (HwangsaeTestStreamer * self)
{
  self->uri = g_strdup ("srt://127.0.0.1:8888?mode=listener");
  self->username = g_strdup_printf ("HwangsaeTestStreamer_%p", self);
}

static void
hwangsae_test_streamer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  HwangsaeTestStreamer *self = HWANGSAE_TEST_STREAMER (object);

  switch (prop_id) {
    case PROP_USERNAME:
      g_value_set_string (value, self->username);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
hwangsae_test_streamer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  HwangsaeTestStreamer *self = HWANGSAE_TEST_STREAMER (object);

  switch (prop_id) {
    case PROP_RESOLUTION:
      self->resolution = g_value_get_enum (value);
      break;
    case PROP_USERNAME:
      g_clear_pointer (&self->username, g_free);
      self->username = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
hwangsae_test_streamer_dispose (GObject * object)
{
  HwangsaeTestStreamer *self = HWANGSAE_TEST_STREAMER (object);

  if (self->should_stream) {
    hwangsae_test_streamer_stop (self);
  }

  g_clear_pointer (&self->uri, g_free);
  g_clear_pointer (&self->username, g_free);
  g_clear_object (&self->pipeline);

  G_OBJECT_CLASS (hwangsae_test_streamer_parent_class)->dispose (object);
}

static void
hwangsae_test_streamer_class_init (HwangsaeTestStreamerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = hwangsae_test_streamer_get_property;
  gobject_class->set_property = hwangsae_test_streamer_set_property;
  gobject_class->dispose = hwangsae_test_streamer_dispose;

  g_object_class_install_property (gobject_class, PROP_RESOLUTION,
      g_param_spec_enum ("resolution", "Video resolution", "Video resolution",
          GAEGULI_TYPE_VIDEO_RESOLUTION, GAEGULI_VIDEO_RESOLUTION_640X480,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_USERNAME,
      g_param_spec_string ("username", "SRT username", "SRT username",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

HwangsaeTestStreamer *
hwangsae_test_streamer_new (void)
{
  return g_object_new (HWANGSAE_TYPE_TEST_STREAMER, NULL);
}
