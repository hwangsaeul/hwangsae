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

#include "test.h"

#include <gst/pbutils/pbutils.h>

GstClockTime
hwangsae_test_get_file_duration (const gchar * file_path)
{
  g_autoptr (GstDiscoverer) discoverer = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GstDiscovererInfo) info = NULL;
  g_autoptr (GstDiscovererStreamInfo) stream_info = NULL;
  g_autoptr (GstCaps) stream_caps = NULL;
  g_autofree gchar *stream_caps_str = NULL;
  g_autofree gchar *uri = NULL;
  const gchar *container_type;

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

  if (g_str_has_suffix (file_path, ".mp4")) {
    container_type = "video/quicktime";
  } else if (g_str_has_suffix (file_path, ".ts")) {
    container_type = "video/mpegts";
  } else {
    g_assert_not_reached ();
  }

  g_assert_cmpstr
      (gst_structure_get_name (gst_caps_get_structure (stream_caps, 0)), ==,
      container_type);

  return gst_discoverer_info_get_duration (info);
}
