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

typedef struct
{
  GMainLoop *loop;
  gboolean has_initial_segment;
  GstClockTime gap_start;
  GstClockTime gap_end;
} CheckGapsData;

static GstPadProbeReturn
gap_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  CheckGapsData *data = user_data;

  if (GST_IS_EVENT (info->data)) {
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_SEGMENT:{
        if (data->has_initial_segment) {
          const GstSegment *segment;

          gst_event_parse_segment (event, &segment);
          g_debug ("Segment event at %" GST_TIME_FORMAT,
              GST_TIME_ARGS (segment->position));

          g_assert_cmpuint (data->gap_start, ==, GST_CLOCK_TIME_NONE);
          data->gap_start = segment->position;
        } else {
          // Ignore the segment event at the beginning of the recording.
          data->has_initial_segment = TRUE;
        }
        break;
      }
      case GST_EVENT_EOS:
        g_main_loop_quit (data->loop);
        break;
      default:
        break;
    }
  } else if (GST_IS_BUFFER (info->data)) {
    if (data->gap_start != GST_CLOCK_TIME_NONE &&
        data->gap_end == GST_CLOCK_TIME_NONE) {
      GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
      data->gap_end = GST_BUFFER_PTS (buffer);
    }
  }

  return GST_PAD_PROBE_OK;
}

GstClockTimeDiff
hwangsae_test_get_gap_duration (const gchar * file_path)
{
  g_autoptr (GMainContext) context = g_main_context_new ();
  g_autoptr (GMainLoop) loop = g_main_loop_new (context, FALSE);
  g_autoptr (GstElement) pipeline = NULL;
  g_autoptr (GstElement) sink = NULL;
  g_autoptr (GstPad) pad = NULL;
  g_autofree gchar *pipeline_str = NULL;
  CheckGapsData data = { 0 };
  g_autoptr (GError) error = NULL;

  g_main_context_push_thread_default (context);

  pipeline_str =
      g_strdup_printf ("filesrc location=%s ! decodebin ! fakesink name=sink",
      file_path);

  pipeline = gst_parse_launch (pipeline_str, &error);
  g_assert_no_error (error);

  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  g_assert_nonnull (sink);
  pad = gst_element_get_static_pad (sink, "sink");
  g_assert_nonnull (pad);

  data.loop = loop;
  data.gap_start = data.gap_end = GST_CLOCK_TIME_NONE;
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM, gap_probe_cb,
      &data, NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_main_context_pop_thread_default (context);

  return GST_CLOCK_DIFF (data.gap_start, data.gap_end);
}

gchar *
hwangsae_test_build_source_uri (HwangsaeTestStreamer * streamer,
    HwangsaeRelay * relay, const gchar * username)
{
  g_autofree gchar *streamid = NULL;
  g_autofree gchar *source_uri = NULL;
  g_autofree gchar *stream_caps_str = NULL;
  const gchar *resource;

  g_object_get (streamer, "username", &resource, NULL);
  streamid = g_strdup_printf ("#!::u=%s,r=%s", username, resource);
  streamid = g_uri_escape_string (streamid, NULL, FALSE);

  return g_strdup_printf ("%s?streamid=%s",
      hwangsae_relay_get_source_uri (relay), streamid);
}

GstElement *
hwangsae_test_make_receiver (HwangsaeTestStreamer * streamer,
    HwangsaeRelay * relay, const gchar * username)
{
  g_autofree gchar *uri = NULL;
  g_autofree gchar *pipeline = NULL;
  g_autoptr (GstElement) receiver = NULL;
  g_autoptr (GError) error = NULL;

  uri = hwangsae_test_build_source_uri (streamer, relay, username);
  pipeline = g_strdup_printf ("srtsrc uri=%s ! fakesink", uri);
  receiver = gst_parse_launch (pipeline, &error);
  g_assert_no_error (error);

  gst_element_set_state (receiver, GST_STATE_PLAYING);

  return g_steal_pointer (&receiver);
}
