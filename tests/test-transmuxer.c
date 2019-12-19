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

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  /* Don't treat warnings as fatal, which is GTest default. */
  g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);

  g_test_add_func ("/hwangsae/transmuxer-15s-with-gap", test_15s_with_gap);

  return g_test_run ();
}
