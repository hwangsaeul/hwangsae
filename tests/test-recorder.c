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
#include <glib/gstdio.h>

#include "hwangsae/hwangsae.h"
#include "common/test.h"
#include "common/test-streamer.h"

typedef struct
{
  GMainLoop *loop;
  HwangsaeTestStreamer *streamer;
  HwangsaeRecorder *recorder;

  gboolean should_stream;
  GThread *streaming_thread;
} TestFixture;

static void
fixture_setup (TestFixture * fixture, gconstpointer unused)
{
  g_autoptr (GError) error = NULL;

  fixture->loop = g_main_loop_new (NULL, FALSE);
  fixture->streamer = hwangsae_test_streamer_new ();
  fixture->recorder = hwangsae_recorder_new ();
  g_object_set (fixture->recorder, "recording-dir", "/tmp", NULL);

  g_assert_no_error (error);
}

static void
fixture_teardown (TestFixture * fixture, gconstpointer unused)
{
  g_clear_object (&fixture->streamer);
  g_clear_object (&fixture->recorder);
  g_clear_pointer (&fixture->loop, g_main_loop_unref);
}

// recorder-record -------------------------------------------------------------

typedef struct
{
  TestFixture *fixture;
  gboolean got_file_created_signal;
  gboolean got_file_completed_signal;
} RecorderTestData;

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
file_created_cb (HwangsaeRecorder * recorder, RecorderTestData * data)
{
  g_debug ("A recording file has been opened");

  g_assert_false (data->got_file_created_signal);
  data->got_file_created_signal = TRUE;
}

static void
file_completed_cb (HwangsaeRecorder * recorder, const gchar * file_path,
    RecorderTestData * data)
{
  GstClockTime duration = hwangsae_test_get_file_duration (file_path);

  g_debug ("Finished recording %s, duration %" GST_TIME_FORMAT, file_path,
      GST_TIME_ARGS (duration));

  g_assert_cmpint (labs (GST_CLOCK_DIFF (duration, 5 * GST_SECOND)), <=,
      GST_SECOND);

  g_assert_false (data->got_file_completed_signal);
  data->got_file_completed_signal = TRUE;
}

static void
stream_disconnected_cb (HwangsaeRecorder * recorder, TestFixture * fixture)
{
  g_debug ("Stream disconnected");

  hwangsae_test_streamer_stop (fixture->streamer);

  g_main_loop_quit (fixture->loop);
}

static void
test_recorder_record (TestFixture * fixture, gconstpointer data)
{
  HwangsaeContainer container = GPOINTER_TO_INT (data);
  RecorderTestData test_data = { 0 };
  g_autoptr (GError) error = NULL;

  test_data.fixture = fixture;

  hwangsae_recorder_set_container (fixture->recorder, container);

  g_signal_connect (fixture->recorder, "stream-connected",
      (GCallback) stream_connected_cb, fixture);
  g_signal_connect (fixture->recorder, "file-created",
      (GCallback) file_created_cb, &test_data);
  g_signal_connect (fixture->recorder, "file-completed",
      (GCallback) file_completed_cb, &test_data);
  g_signal_connect (fixture->recorder, "stream-disconnected",
      (GCallback) stream_disconnected_cb, fixture);

  hwangsae_test_streamer_start (fixture->streamer);

  hwangsae_recorder_start_recording (fixture->recorder, "srt://127.0.0.1:8888");

  g_main_loop_run (fixture->loop);

  g_assert_true (test_data.got_file_created_signal);
  g_assert_true (test_data.got_file_completed_signal);
}

// recorder-disconnect ---------------------------------------------------------

const guint SEGMENT_LEN_SECONDS = 5;

static void
recording_done_cb (HwangsaeRecorder * recorder, const gchar * file_path,
    TestFixture * fixture)
{
  GstClockTime duration;
  GstClockTimeDiff gap;
  const GstClockTime expected_duration = 3 * SEGMENT_LEN_SECONDS * GST_SECOND;
  const GstClockTime expected_gap = SEGMENT_LEN_SECONDS * GST_SECOND;

  hwangsae_test_streamer_stop (fixture->streamer);

  duration = hwangsae_test_get_file_duration (file_path);

  g_debug ("Finished recording %s, duration %" GST_TIME_FORMAT, file_path,
      GST_TIME_ARGS (duration));

  g_assert_cmpint (labs (GST_CLOCK_DIFF (duration, expected_duration)), <=,
      2 * GST_SECOND);

  gap = hwangsae_test_get_gap_duration (file_path);

  g_debug ("Gap in the file lasts %" GST_STIME_FORMAT, GST_STIME_ARGS (gap));

  g_assert_cmpint (labs (GST_CLOCK_DIFF (gap, expected_gap)), <=, GST_SECOND);
}

static gboolean
second_segment_done_cb (TestFixture * fixture)
{
  g_debug ("Second segment done.");
  hwangsae_recorder_stop_recording (fixture->recorder);

  return G_SOURCE_REMOVE;
}

static gboolean
second_segment_started_cb (TestFixture * fixture)
{
  g_debug ("Recording second segment of %u seconds.", SEGMENT_LEN_SECONDS);
  hwangsae_test_streamer_start (fixture->streamer);
  g_timeout_add_seconds (SEGMENT_LEN_SECONDS,
      (GSourceFunc) second_segment_done_cb, fixture);

  return G_SOURCE_REMOVE;
}

static gboolean
first_segment_done_cb (TestFixture * fixture)
{
  g_debug ("First segment done. Stopping streaming for %u seconds.",
      SEGMENT_LEN_SECONDS);
  hwangsae_test_streamer_pause (fixture->streamer);
  g_timeout_add_seconds (SEGMENT_LEN_SECONDS,
      (GSourceFunc) second_segment_started_cb, fixture);

  return G_SOURCE_REMOVE;
}

static void
first_segment_started_cb (HwangsaeRecorder * recorder, TestFixture * fixture)
{
  g_debug ("Recording first segment of 5 seconds.");
  g_timeout_add_seconds (SEGMENT_LEN_SECONDS,
      (GSourceFunc) first_segment_done_cb, fixture);
}

static void
test_recorder_disconnect (TestFixture * fixture, gconstpointer unused)
{
  g_signal_connect (fixture->recorder, "stream-connected",
      (GCallback) first_segment_started_cb, fixture);
  g_signal_connect (fixture->recorder, "file-completed",
      (GCallback) recording_done_cb, fixture);
  g_signal_connect_swapped (fixture->recorder, "stream-disconnected",
      (GCallback) g_main_loop_quit, fixture->loop);

  hwangsae_test_streamer_start (fixture->streamer);

  hwangsae_recorder_start_recording (fixture->recorder, "srt://127.0.0.1:8888");

  g_main_loop_run (fixture->loop);
}

// recorder-split --------------------------------------------------------------

const guint NUM_FILE_SEGMENTS = 3;

typedef struct
{
  GSList *filenames;
  guint file_created_signal_count;
  guint file_completed_signal_count;
} SplitData;

static void
split_file_created_cb (HwangsaeRecorder * recorder, SplitData * data)
{
  g_debug ("A recording file has been opened");

  if (++data->file_created_signal_count == NUM_FILE_SEGMENTS) {
    hwangsae_recorder_stop_recording (recorder);
  }
}

static void
split_file_completed_cb (HwangsaeRecorder * recorder,
    const gchar * file_path, SplitData * data)
{
  g_debug ("Completed file %s", file_path);

  data->filenames = g_slist_append (data->filenames, g_strdup (file_path));
  ++data->file_completed_signal_count;
}

static GSList *
split_run_test (TestFixture * fixture)
{
  SplitData data = { 0 };

  g_signal_connect (fixture->recorder, "file-created",
      (GCallback) split_file_created_cb, &data);
  g_signal_connect (fixture->recorder, "file-completed",
      (GCallback) split_file_completed_cb, &data);
  g_signal_connect (fixture->recorder, "stream-disconnected",
      (GCallback) stream_disconnected_cb, fixture);

  hwangsae_test_streamer_start (fixture->streamer);

  hwangsae_recorder_start_recording (fixture->recorder, "srt://127.0.0.1:8888");

  g_main_loop_run (fixture->loop);

  g_assert_cmpint (data.file_created_signal_count, >=, NUM_FILE_SEGMENTS);
  g_assert_cmpint (data.file_completed_signal_count, ==,
      data.file_created_signal_count);
  g_assert_cmpint (g_slist_length (data.filenames), >=, NUM_FILE_SEGMENTS);

  return data.filenames;
}

static void
test_recorder_split_time (TestFixture * fixture, gconstpointer unused)
{
  GSList *filenames;
  const GstClockTimeDiff FILE_SEGMENT_LEN = 5 * GST_SECOND;

  hwangsae_recorder_set_max_size_time (fixture->recorder, FILE_SEGMENT_LEN);

  filenames = split_run_test (fixture);

  for (; filenames; filenames = g_slist_delete_link (filenames, filenames)) {
    g_autofree gchar *filename = filenames->data;
    GstClockTime duration = hwangsae_test_get_file_duration (filename);

    g_debug ("%s has duration %" GST_TIME_FORMAT, filename,
        GST_TIME_ARGS (duration));

    if (filenames->next) {
      g_assert_cmpint (labs (GST_CLOCK_DIFF (duration, FILE_SEGMENT_LEN)), <=,
          GST_SECOND);
    } else {
      /* The final segment should be shorter than FILE_SEGMENT_LEN. */
      g_assert_cmpint (duration, <, FILE_SEGMENT_LEN);
    }
  }
}

static void
test_recorder_split_bytes (TestFixture * fixture, gconstpointer unused)
{
  GSList *filenames;
  guint64 FILE_SEGMENT_LEN_BYTES = 5e6;

  hwangsae_recorder_set_max_size_bytes (fixture->recorder,
      FILE_SEGMENT_LEN_BYTES);

  filenames = split_run_test (fixture);

  for (; filenames; filenames = g_slist_delete_link (filenames, filenames)) {
    g_autofree gchar *filename = filenames->data;
    GStatBuf stat_buf;

    g_assert (g_stat (filename, &stat_buf) == 0);

    g_debug ("%s has size %luB", filename, stat_buf.st_size);

    if (filenames->next) {
      g_assert_cmpint (labs (stat_buf.st_size - FILE_SEGMENT_LEN_BYTES), <=,
          FILE_SEGMENT_LEN_BYTES / 5);
    } else {
      /* The final segment should be shorter than FILE_SEGMENT_LEN_BYTES. */
      g_assert_cmpint (stat_buf.st_size, <, FILE_SEGMENT_LEN_BYTES);
    }
  }
}

static gboolean
stop_recording_no_streamer_cb (HwangsaeRecorder * recorder)
{
  /* Stop the recorder before anything has connected. */
  hwangsae_recorder_stop_recording (recorder);

  return G_SOURCE_REMOVE;
}

static void
test_recorder_stop_no_streamer (TestFixture * fixture, gconstpointer unused)
{
  g_signal_connect (fixture->recorder, "stream-disconnected",
      (GCallback) stream_disconnected_cb, fixture);

  hwangsae_recorder_start_recording (fixture->recorder, "srt://127.0.0.1:2222");

  g_timeout_add_seconds (1, (GSourceFunc) stop_recording_no_streamer_cb,
      fixture->recorder);

  g_main_loop_run (fixture->loop);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  /* Don't treat warnings as fatal, which is GTest default. */
  g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);

  g_test_add ("/hwangsae/recorder-record-mp4",
      TestFixture, GUINT_TO_POINTER (HWANGSAE_CONTAINER_MP4), fixture_setup,
      test_recorder_record, fixture_teardown);

  g_test_add ("/hwangsae/recorder-record-ts",
      TestFixture, GUINT_TO_POINTER (HWANGSAE_CONTAINER_TS), fixture_setup,
      test_recorder_record, fixture_teardown);

  g_test_add ("/hwangsae/recorder-disconnect",
      TestFixture, NULL, fixture_setup,
      test_recorder_disconnect, fixture_teardown);

  g_test_add ("/hwangsae/recorder-split-time",
      TestFixture, NULL, fixture_setup,
      test_recorder_split_time, fixture_teardown);

  g_test_add ("/hwangsae/recorder-split-bytes",
      TestFixture, NULL, fixture_setup,
      test_recorder_split_bytes, fixture_teardown);

  g_test_add ("/hwangsae/recorder-stop-no-streamer",
      TestFixture, NULL, fixture_setup,
      test_recorder_stop_no_streamer, fixture_teardown);

  return g_test_run ();
}
