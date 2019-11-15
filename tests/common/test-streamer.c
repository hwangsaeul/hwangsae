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

struct _HwangsaeTestStreamer
{
  GObject parent;

  GaeguliFifoTransmit *transmit;
  GaeguliPipeline *pipeline;

  gboolean should_stream;
  GThread *streaming_thread;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (HwangsaeTestStreamer, hwangsae_test_streamer, G_TYPE_OBJECT)
/* *INDENT-ON* */

static gboolean
hwangsae_test_streamer_thread_func (HwangsaeTestStreamer * self)
{
  g_autoptr (GMainContext) context = g_main_context_new ();
  g_autoptr (GError) error = NULL;
  guint transmit_id;

  g_main_context_push_thread_default (context);

  transmit_id = gaeguli_fifo_transmit_start (self->transmit,
      "127.0.0.1", 8888, GAEGULI_SRT_MODE_LISTENER, &error);
  g_assert_no_error (error);

  while (self->should_stream) {
    g_main_context_iteration (context, TRUE);
  }

  gaeguli_fifo_transmit_stop (self->transmit, transmit_id, &error);
  g_assert_no_error (error);

  return TRUE;
}

void
hwangsae_test_streamer_start (HwangsaeTestStreamer * self)
{
  g_assert_null (self->streaming_thread);

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
  g_autoptr (GError) error = NULL;

  self->transmit = gaeguli_fifo_transmit_new ();
  self->pipeline = gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC,
      NULL, GAEGULI_ENCODING_METHOD_GENERAL);

  g_object_set (self->pipeline, "clock-overlay", TRUE, NULL);

  gaeguli_pipeline_add_fifo_target_full (self->pipeline,
      GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_640X480,
      gaeguli_fifo_transmit_get_fifo (self->transmit), &error);
  g_assert_no_error (error);
}

static void
hwangsae_test_streamer_dispose (GObject * object)
{
  HwangsaeTestStreamer *self = HWANGSAE_TEST_STREAMER (object);

  if (self->should_stream) {
    hwangsae_test_streamer_stop (self);
  }

  g_clear_object (&self->transmit);
  g_clear_object (&self->pipeline);

  G_OBJECT_CLASS (hwangsae_test_streamer_parent_class)->dispose (object);
}

static void
hwangsae_test_streamer_class_init (HwangsaeTestStreamerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = hwangsae_test_streamer_dispose;
}

HwangsaeTestStreamer *
hwangsae_test_streamer_new (void)
{
  return g_object_new (HWANGSAE_TYPE_TEST_STREAMER, NULL);
}
