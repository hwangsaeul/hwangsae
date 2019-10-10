/** 
 *  tests/test-recorder
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

#include <gaeguli/gaeguli.h>
#include <gio/gio.h>
#include <gst/pbutils/pbutils.h>

#include "hwangsae/hwangsae.h"

typedef struct
{
  GMainLoop *loop;
  GaeguliFifoTransmit *transmit;
  GaeguliPipeline *pipeline;
  HwangsaeRecorder *recorder;

  gboolean should_stream;
  GThread *streaming_thread;
} TestFixture;

static void
fixture_setup (TestFixture * fixture, gconstpointer unused)
{
  g_autoptr (GError) error = NULL;

  fixture->loop = g_main_loop_new (NULL, FALSE);
  fixture->transmit = gaeguli_fifo_transmit_new ();
  fixture->pipeline = gaeguli_pipeline_new ();
  fixture->recorder = hwangsae_recorder_new ();
  g_object_set (fixture->recorder, "recording-dir", "/tmp", NULL);

  gaeguli_pipeline_add_fifo_target_full (fixture->pipeline,
      GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_640x480,
      gaeguli_fifo_transmit_get_fifo (fixture->transmit), &error);
  g_assert_no_error (error);
}

static void
fixture_teardown (TestFixture * fixture, gconstpointer unused)
{
  g_clear_object (&fixture->recorder);
  g_clear_object (&fixture->transmit);
  g_clear_object (&fixture->pipeline);
  g_clear_pointer (&fixture->loop, g_main_loop_unref);
}

static gboolean
streaming_thread_func (TestFixture * fixture)
{
  g_autoptr (GMainContext) context = g_main_context_new ();
  g_autoptr (GError) error = NULL;
  guint transmit_id;

  g_main_context_push_thread_default (context);

  transmit_id = gaeguli_fifo_transmit_start (fixture->transmit,
      "127.0.0.1", 8888, GAEGULI_SRT_MODE_LISTENER, &error);
  g_assert_no_error (error);

  while (fixture->should_stream) {
    g_main_context_iteration (context, TRUE);
  }

  gaeguli_fifo_transmit_stop (fixture->transmit, transmit_id, &error);
  g_assert_no_error (error);

  return TRUE;
}

static void
start_streaming (TestFixture * fixture)
{
  g_assert_null (fixture->streaming_thread);

  fixture->should_stream = TRUE;
  fixture->streaming_thread = g_thread_new ("streaming_thread_func",
      (GThreadFunc) streaming_thread_func, fixture);
}

static void
stop_streaming (TestFixture * fixture)
{
  g_assert_nonnull (fixture->streaming_thread);

  fixture->should_stream = FALSE;
  g_clear_pointer (&fixture->streaming_thread, g_thread_join);
}

static GstClockTime
get_file_duration (const gchar * file_path)
{
  g_autoptr (GstDiscoverer) discoverer = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GstDiscovererInfo) info = NULL;
  g_autoptr (GstDiscovererStreamInfo) stream_info = NULL;
  g_autoptr (GstCaps) stream_caps = NULL;
  g_autofree gchar *stream_caps_str = NULL;
  g_autofree gchar *uri = NULL;

  discoverer = gst_discoverer_new (5 * GST_SECOND, &error);
  g_assert_no_error (error);

  uri = g_strdup_printf ("file://%s", file_path);

  info = gst_discoverer_discover_uri (discoverer, uri, &error);
  g_assert_no_error (error);
  g_assert_cmpint (gst_discoverer_info_get_result (info), ==,
      GST_DISCOVERER_OK);

  stream_info = gst_discoverer_info_get_stream_info (info);
  stream_caps = gst_discoverer_stream_info_get_caps (stream_info);

  stream_caps_str = gst_caps_to_string (stream_caps);
  g_debug ("Container file has caps: %s", stream_caps_str);

  g_assert_cmpint (gst_caps_get_size (stream_caps), ==, 1);
  g_assert_cmpstr
      (gst_structure_get_name (gst_caps_get_structure (stream_caps, 0)), ==,
      "video/quicktime");

  return gst_discoverer_info_get_duration (info);
}

static gboolean
stop_recording_timeout_cb (TestFixture * fixture)
{
  hwangsae_recorder_stop_recording (fixture->recorder);

  return G_SOURCE_REMOVE;
}

static void
stream_connected_cb (HwangsaeRecorder * recorder, TestFixture * fixture)
{
  g_debug ("Stream connected");
  g_timeout_add_seconds (5, (GSourceFunc) stop_recording_timeout_cb, fixture);
}

static void
file_completed_cb (HwangsaeRecorder * recorder, const gchar * file_path,
    TestFixture * fixture)
{
  GstClockTime duration = get_file_duration (file_path);

  g_debug ("Finished recording %s, duration %" GST_TIME_FORMAT, file_path,
      GST_TIME_ARGS (duration));

  g_assert_cmpint (labs (GST_CLOCK_DIFF (duration, 5 * GST_SECOND)), <=,
      GST_SECOND);
}

static void
stream_disconnected_cb (HwangsaeRecorder * recorder, TestFixture * fixture)
{
  g_debug ("Stream disconnected");

  gaeguli_pipeline_stop (fixture->pipeline);
  stop_streaming (fixture);

  g_main_loop_quit (fixture->loop);
}

static void
test_hwangsae_recorder_record (TestFixture * fixture, gconstpointer unused)
{
  g_autoptr (GError) error = NULL;

  g_signal_connect (fixture->recorder, "stream-connected",
      (GCallback) stream_connected_cb, fixture);
  g_signal_connect (fixture->recorder, "file-completed",
      (GCallback) file_completed_cb, fixture);
  g_signal_connect (fixture->recorder, "stream-disconnected",
      (GCallback) stream_disconnected_cb, fixture);

  start_streaming (fixture);

  hwangsae_recorder_start_recording (fixture->recorder, "srt://127.0.0.1:8888");

  g_main_loop_run (fixture->loop);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  /* Don't treat warnings as fatal, which is GTest default. */
  g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);

  g_test_add ("/hwangsae/recorder-record",
      TestFixture, NULL, fixture_setup,
      test_hwangsae_recorder_record, fixture_teardown);

  return g_test_run ();
}
