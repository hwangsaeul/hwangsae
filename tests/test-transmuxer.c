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

  input_files = g_slist_append (input_files,
      g_test_build_filename (G_TEST_DIST, "data/test-0-5000000.ts", NULL));

  input_files = g_slist_append (input_files,
      g_test_build_filename (G_TEST_DIST,
          "data/test-5000000-10000000-corrupted.ts", NULL));

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

  g_slist_free_full (input_files, g_free);

  g_unlink (output_file);
}

static void
_rmdir_non_empty (gchar * folder)
{
  g_autoptr (GDir) dir = NULL;
  g_autoptr (GError) error = NULL;
  const gchar *filename = NULL;
  g_autofree gchar *filepath = NULL;

  dir = g_dir_open (folder, 0, &error);
  if (error)
    return;

  while ((filename = g_dir_read_name (dir))) {
    filepath = g_build_filename (folder, filename, NULL);
    g_unlink (filepath);
  }

  g_rmdir (folder);

}

static gint
gchar_compare (gconstpointer a, gconstpointer b)
{
  gchar *str_a = *(gchar **) a;
  gchar *str_b = *(gchar **) b;

  return g_strcmp0 (str_a, str_b);
}

static void
test_split_time_bytes (gint64 time, gint64 bytes)
{
  g_autoptr (HwangsaeTransmuxer) transmuxer = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *output_folder = NULL;
  g_autofree gchar *output_file = NULL;
  GSList *input_files = NULL;
  g_autoptr (GArray) file_list = NULL;
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

  transmuxer = hwangsae_transmuxer_new ();

  output_folder =
      g_build_filename (g_get_tmp_dir (), "transmuxer-test-XXXXXX", NULL);
  g_mkdtemp (output_folder);

  g_debug ("using folder %s\n", output_folder);

  output_file = g_build_filename (output_folder, "%05d.mp4", NULL);

  if (time)
    hwangsae_transmuxer_set_max_size_time (transmuxer, time);
  else if (bytes)
    hwangsae_transmuxer_set_max_size_bytes (transmuxer, bytes);

  hwangsae_transmuxer_merge (transmuxer, input_files, output_file, &error);

  g_assert_no_error (error);

  g_slist_free_full (input_files, g_free);

  // Check if the splitting is OK
  dir = g_dir_open (output_folder, 0, &error);

  g_assert_no_error (error);

  file_list = g_array_new (TRUE, TRUE, sizeof (gchar *));
  while ((filename = g_dir_read_name (dir))) {
    g_array_append_val (file_list, filename);
  }

  g_array_sort (file_list, gchar_compare);

  for (gint i = 0; i < file_list->len; i++) {
    g_autofree gchar *filepath = NULL;

    filename = g_array_index (file_list, gchar *, i);
    filepath = g_build_filename (output_folder, filename, NULL);

    g_debug ("Checking file %s", filename);
    if (time) {
      GstClockTime duration = hwangsae_test_get_file_duration (filepath);

      g_debug ("%s has duration %" GST_TIME_FORMAT, filename,
          GST_TIME_ARGS (duration));
      if (i < file_list->len - 1) {
        g_assert_cmpint (labs (GST_CLOCK_DIFF (duration, time)), <=,
            GST_SECOND);
      } else {
        /* The final segment shouldn't be longer than FILE_SEGMENT_LEN. */
        g_assert_cmpint (duration, <=, time);
      }
    } else if (bytes) {
      GStatBuf stat_buf;

      g_assert (g_stat (filepath, &stat_buf) == 0);

      g_debug ("%s has size %luB", filepath, stat_buf.st_size);

      if (i < file_list->len - 1) {
        g_assert_cmpint (labs (stat_buf.st_size - bytes), <=, bytes / 5);
      } else {
        /* The final segment shouldn't be longer than FILE_SEGMENT_LEN_BYTES. */
        g_assert_cmpint (stat_buf.st_size, <=, bytes);
      }
    }
  }

  _rmdir_non_empty (output_folder);
}

static void
test_split_time (void)
{
  test_split_time_bytes (3 * GST_SECOND, 0);
}

static void
test_split_bytes (void)
{
  test_split_time_bytes (0, 1 * 1024 * 1024);
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


  return g_test_run ();
}
