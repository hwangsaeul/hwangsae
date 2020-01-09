/** 
 *  tests/test-relay
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

#include "hwangsae/hwangsae.h"

#include "common/test.h"

#include <glib/gstdio.h>
#include <gst/gstclock.h>

static void
test_15s_with_gap (void)
{
  g_autoptr (HwangsaeTransmuxer) transmuxer = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *output_file = NULL;
  GstClockTimeDiff gap;
  GSList *input_files = NULL;

  input_files = g_slist_append (input_files,
      g_test_build_filename (G_TEST_DIST, "data/test-0-5000000.ts", NULL));
  input_files = g_slist_append (input_files,
      g_test_build_filename (G_TEST_DIST, "data/test-10000000-15000000.ts",
          NULL));

  transmuxer = hwangsae_transmuxer_new ();

  output_file =
      g_build_filename (g_get_tmp_dir (), "transmuxer-test-XXXXXX.mp4", NULL);
  g_close (g_mkstemp (output_file), NULL);

  hwangsae_transmuxer_merge (transmuxer, input_files, output_file, &error);

  g_assert_no_error (error);

  g_slist_free_full (input_files, g_free);

  /* Total file duration should be 15 seconds. */
  g_assert_cmpuint (hwangsae_test_get_file_duration (output_file), ==,
      15 * GST_SECOND);

  /* Gap should last 5 seconds ±20ms. */
  gap = hwangsae_test_get_gap_duration (output_file);
  g_assert_cmpint (labs (GST_CLOCK_DIFF (gap, 5 * GST_SECOND)), <=,
      20 * GST_MSECOND);

  g_unlink (output_file);
}

static void
test_corrupted (void)
{
  g_autoptr (HwangsaeTransmuxer) transmuxer = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *output_file = NULL;
  GSList *input_files = NULL;
  GstClockTimeDiff gap;

  input_files = g_slist_append (input_files,
      g_test_build_filename (G_TEST_DIST, "data/test-0-5000000.ts", NULL));

  input_files = g_slist_append (input_files,
      g_test_build_filename (G_TEST_DIST,
          "data/test-corrupted-5000000-10000000.ts", NULL));

  input_files =
      g_slist_append (input_files, g_test_build_filename (G_TEST_DIST,
          "data/test-10000000-15000000.ts", NULL));

  transmuxer = hwangsae_transmuxer_new ();

  output_file =
      g_build_filename (g_get_tmp_dir (), "transmuxer-test-XXXXXX.mp4", NULL);
  g_close (g_mkstemp (output_file), NULL);

  hwangsae_transmuxer_merge (transmuxer, input_files, output_file, &error);

  g_assert_no_error (error);

  /* Total file duration should be 15 seconds. */
  g_assert_cmpuint (hwangsae_test_get_file_duration (output_file), ==,
      15 * GST_SECOND);

  /* Gap should last 5 seconds ±20ms. */
  gap = hwangsae_test_get_gap_duration (output_file);
  g_assert_cmpint (labs (GST_CLOCK_DIFF (gap, 5 * GST_SECOND)), <=,
      20 * GST_MSECOND);

  g_slist_free_full (input_files, g_free);

  g_unlink (output_file);
}

static gint
gchar_compare (gconstpointer a, gconstpointer b)
{
  return g_strcmp0 ((gchar *) a, (gchar *) b);
}

static GSList *
test_split (HwangsaeTransmuxer * transmuxer)
{
  g_autoptr (GError) error = NULL;
  g_autofree gchar *output_folder = NULL;
  g_autofree gchar *output_file = NULL;
  GSList *input_files = NULL;
  GSList *output_files = NULL;
  g_autoptr (GDir) dir = NULL;
  const gchar *filename = NULL;

  input_files = g_slist_append (input_files,
      g_test_build_filename (G_TEST_DIST, "data/test-0-5000000.ts", NULL));
  input_files = g_slist_append (input_files,
      g_test_build_filename (G_TEST_DIST, "data/test-5000000-10000000.ts",
          NULL));
  input_files = g_slist_append (input_files,
      g_test_build_filename (G_TEST_DIST, "data/test-10000000-15000000.ts",
          NULL));

  output_folder =
      g_build_filename (g_get_tmp_dir (), "transmuxer-test-XXXXXX", NULL);
  g_mkdtemp (output_folder);

  g_debug ("using folder %s\n", output_folder);

  output_file = g_build_filename (output_folder, "%05d.mp4", NULL);

  hwangsae_transmuxer_merge (transmuxer, input_files, output_file, &error);

  g_assert_no_error (error);

  g_slist_free_full (input_files, g_free);

  // Check if the splitting is OK
  dir = g_dir_open (output_folder, 0, &error);

  g_assert_no_error (error);

  while ((filename = g_dir_read_name (dir))) {
    output_files = g_slist_insert_sorted (output_files,
        g_build_filename (output_folder, filename, NULL), gchar_compare);
  }

  return output_files;
}

static void
test_split_time (void)
{
  const GstClockTime SPLIT_TIME = 3 * GST_SECOND;

  g_autoptr (HwangsaeTransmuxer) transmuxer = hwangsae_transmuxer_new ();
  GSList *files;
  GSList *it;

  hwangsae_transmuxer_set_max_size_time (transmuxer, SPLIT_TIME);

  files = test_split (transmuxer);

  for (it = files; it; it = it->next) {
    gchar *filepath = it->data;
    g_autofree gchar *filename = g_path_get_basename (filepath);
    GstClockTime duration;

    duration = hwangsae_test_get_file_duration (filepath);
    g_debug ("%s has duration %" GST_TIME_FORMAT, filename,
        GST_TIME_ARGS (duration));

    if (it->next) {
      g_assert_cmpint (labs (GST_CLOCK_DIFF (duration, SPLIT_TIME)), <=,
          GST_SECOND);
    } else {
      /* The final segment shouldn't be longer than SPLIT_TIME. */
      g_assert_cmpint (duration, <=, SPLIT_TIME);
    }

    g_unlink (filepath);
  }

  if (files) {
    g_autofree gchar *dirname = g_path_get_dirname (files->data);
    g_rmdir (dirname);
  }

  g_slist_free_full (files, g_free);
}

static void
test_split_bytes (void)
{
  const guint64 SPLIT_BYTES = 1 * 1024 * 1024;

  g_autoptr (HwangsaeTransmuxer) transmuxer = hwangsae_transmuxer_new ();
  GSList *files;
  GSList *it;

  hwangsae_transmuxer_set_max_size_bytes (transmuxer, SPLIT_BYTES);

  files = test_split (transmuxer);

  for (it = files; it; it = it->next) {
    gchar *filepath = it->data;
    g_autofree gchar *filename = g_path_get_basename (filepath);
    GStatBuf stat_buf;

    g_assert (g_stat (filepath, &stat_buf) == 0);

    g_debug ("%s has size %luB", filename, stat_buf.st_size);

    if (it->next) {
      g_assert_cmpint (labs (stat_buf.st_size - SPLIT_BYTES), <=,
          SPLIT_BYTES / 5);
    } else {
      /* The final segment shouldn't be longer than SPLIT_BYTES. */
      g_assert_cmpint (stat_buf.st_size, <=, SPLIT_BYTES);
    }

    g_unlink (filepath);
  }

  if (files) {
    g_autofree gchar *dirname = g_path_get_dirname (files->data);
    g_rmdir (dirname);
  }

  g_slist_free_full (files, g_free);
}

static void
test_split_ondemand (void)
{
  /* Sum of durations must equal total length of input in test_split (). */
  GstClockTime OUTPUT_SEGMENT_DURATIONS[] = {
    2 * GST_SECOND, 5 * GST_SECOND, 3 * GST_SECOND, 5 * GST_SECOND
  };

  g_autoptr (HwangsaeTransmuxer) transmuxer = hwangsae_transmuxer_new ();
  GstClockTime split_at_time = 0;
  GSList *files;
  GSList *it;
  gint i;

  for (i = 0; i != G_N_ELEMENTS (OUTPUT_SEGMENT_DURATIONS) - 1; ++i) {
    split_at_time += OUTPUT_SEGMENT_DURATIONS[i];
    hwangsae_transmuxer_split_at_running_time (transmuxer, split_at_time);
  }

  files = test_split (transmuxer);

  g_assert_cmpint (g_slist_length (files), ==,
      G_N_ELEMENTS (OUTPUT_SEGMENT_DURATIONS));

  i = 0;
  for (it = files; it; it = it->next) {
    gchar *filepath = it->data;
    g_autofree gchar *filename = g_path_get_basename (filepath);
    GstClockTime duration;

    duration = hwangsae_test_get_file_duration (filepath);
    g_debug ("%s has duration %" GST_TIME_FORMAT, filename,
        GST_TIME_ARGS (duration));

    g_assert_cmpint (duration, ==, OUTPUT_SEGMENT_DURATIONS[i++]);

    g_unlink (filepath);
  }

  if (files) {
    g_autofree gchar *dirname = g_path_get_dirname (files->data);
    g_rmdir (dirname);
  }

  g_slist_free_full (files, g_free);
}

static void
test_overlap (void)
{
  g_autoptr (HwangsaeTransmuxer) transmuxer = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *output_file = NULL;
  GSList *input_files = NULL;

  input_files = g_slist_append (input_files,
      g_test_build_filename (G_TEST_DIST, "data/test-0-5000000.ts", NULL));

  input_files = g_slist_append (input_files,
      g_test_build_filename (G_TEST_DIST, "data/test-5000000-11000000.ts",
          NULL));

  input_files = g_slist_append (input_files,
      g_test_build_filename (G_TEST_DIST, "data/test-10000000-15000000.ts",
          NULL));

  transmuxer = hwangsae_transmuxer_new ();

  output_file =
      g_build_filename (g_get_tmp_dir (), "transmuxer-test-XXXXXX.mp4", NULL);
  g_close (g_mkstemp (output_file), NULL);

  hwangsae_transmuxer_merge (transmuxer, input_files, output_file, &error);

  g_assert_true (g_error_matches (error, HWANGSAE_TRANSMUXER_ERROR,
          HWANGSAE_TRANSMUXER_ERROR_OVERLAP));

  g_slist_free_full (input_files, g_free);

  g_unlink (output_file);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  /* Don't treat warnings as fatal, which is GTest default. */
  g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);

  g_test_add_func ("/hwangsae/transmuxer-15s-with-gap", test_15s_with_gap);

  g_test_add_func ("/hwangsae/transmuxer-corrupted", test_corrupted);

  g_test_add_func ("/hwangsae/transmuxer-split_time", test_split_time);

  g_test_add_func ("/hwangsae/transmuxer-split_bytes", test_split_bytes);

  g_test_add_func ("/hwangsae/transmuxer-split_ondemand", test_split_ondemand);

  g_test_add_func ("/hwangsae/transmuxer-overlap", test_overlap);

  return g_test_run ();
}
