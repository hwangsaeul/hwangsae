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

#include <gaeguli/gaeguli.h>
#include <gst/gsturi.h>

struct _HwangsaeTestStreamer
{
  GObject parent;

  gchar *uri;
  gchar *username;
  GaeguliVideoResolution resolution;

  GaeguliFifoTransmit *transmit;
  GaeguliPipeline *pipeline;

  gboolean should_stream;
  GThread *streaming_thread;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (HwangsaeTestStreamer, hwangsae_test_streamer, G_TYPE_OBJECT)
/* *INDENT-ON* */

enum
{
  PROP_RESOLUTION = 1,
  PROP_LAST
};

static gboolean
hwangsae_test_streamer_thread_func (HwangsaeTestStreamer * self)
{
  g_autoptr (GMainContext) context = g_main_context_new ();
  g_autoptr (GError) error = NULL;
  g_autoptr (GstUri) uri = NULL;
  const gchar *mode_str;
  GaeguliSRTMode mode = GAEGULI_SRT_MODE_UNKNOWN;
  guint transmit_id;

  g_main_context_push_thread_default (context);

  uri = gst_uri_from_string (self->uri);

  mode_str = gst_uri_get_query_value (uri, "mode");
  if (!mode_str || g_str_equal (mode_str, "caller")) {
    mode = GAEGULI_SRT_MODE_CALLER;
  } else if (g_str_equal (mode_str, "listener")) {
    mode = GAEGULI_SRT_MODE_LISTENER;
  }
  g_assert (mode != GAEGULI_SRT_MODE_UNKNOWN);

  transmit_id = gaeguli_fifo_transmit_start_full (self->transmit,
      gst_uri_get_host (uri), gst_uri_get_port (uri), mode, self->username,
      &error);
  g_assert_no_error (error);

  while (self->should_stream) {
    g_main_context_iteration (context, TRUE);
  }

  gaeguli_fifo_transmit_stop (self->transmit, transmit_id, &error);
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
  g_autoptr (GError) error = NULL;

  g_assert_null (self->streaming_thread);

  gaeguli_pipeline_add_fifo_target_full (self->pipeline,
      GAEGULI_VIDEO_CODEC_H264, self->resolution,
      gaeguli_fifo_transmit_get_fifo (self->transmit), &error);
  g_assert_no_error (error);

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
  gaeguli_pipeline_stop (self->pipeline);
  hwangsae_test_streamer_pause (self);
}

static void
hwangsae_test_streamer_init (HwangsaeTestStreamer * self)
{
  self->uri = g_strdup ("srt://127.0.0.1:8888?mode=listener");
  self->username = g_strdup_printf ("HwangsaeTestStreamer %p", self);
  self->transmit = gaeguli_fifo_transmit_new ();
  self->pipeline = gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC,
      NULL, GAEGULI_ENCODING_METHOD_GENERAL);

  g_object_set (self->pipeline, "clock-overlay", TRUE, NULL);
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
  g_clear_object (&self->transmit);
  g_clear_object (&self->pipeline);

  G_OBJECT_CLASS (hwangsae_test_streamer_parent_class)->dispose (object);
}

static void
hwangsae_test_streamer_class_init (HwangsaeTestStreamerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = hwangsae_test_streamer_set_property;
  gobject_class->dispose = hwangsae_test_streamer_dispose;

  g_object_class_install_property (gobject_class, PROP_RESOLUTION,
      g_param_spec_enum ("resolution", "Video resolution", "Video resolution",
          GAEGULI_TYPE_VIDEO_RESOLUTION, GAEGULI_VIDEO_RESOLUTION_640X480,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

HwangsaeTestStreamer *
hwangsae_test_streamer_new (void)
{
  return g_object_new (HWANGSAE_TYPE_TEST_STREAMER, NULL);
}
