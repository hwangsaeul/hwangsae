/** 
 *  tests/test-relay
 *
 *  Copyright 2019 SK Telecom Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
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

#include "hwangsae/common.h"
#include "hwangsae/hwangsae.h"
#include "hwangsae/test/test-streamer.h"

#include <gaeguli/gaeguli.h>
#include <gio/gio.h>
#include <gst/pbutils/gstdiscoverer.h>

static gchar *build_source_uri (HwangsaeTestStreamer * streamer,
    HwangsaeRelay * relay, const gchar * username);

static void
test_relay_instance (void)
{
  guint sink_port, source_port;
  g_autoptr (HwangsaeRelay) relay = hwangsae_relay_new ("", 8888, 9999);

  g_assert_nonnull (relay);

  g_object_get (relay, "sink-port", &sink_port, "source-port", &source_port,
      NULL);

  g_assert_cmpint (sink_port, ==, 8888);
  g_assert_cmpint (source_port, ==, 9999);
}

static void
test_external_ip (void)
{
  static const gchar *EXTERNAL_IP = "10.1.2.3";
  g_autoptr (HwangsaeRelay) relay =
      hwangsae_relay_new (EXTERNAL_IP, 8888, 9999);
  g_autofree gchar *sink_uri = NULL;
  g_autofree gchar *source_uri = NULL;
  guint sink_port, source_port;

  g_object_set (relay, "external-ip", EXTERNAL_IP, NULL);
  g_object_get (relay, "sink-port", &sink_port, "source-port", &source_port,
      NULL);

  sink_uri = g_strdup_printf ("srt://%s:%d", EXTERNAL_IP, sink_port);
  source_uri = g_strdup_printf ("srt://%s:%d", EXTERNAL_IP, source_port);

  g_assert_cmpstr (hwangsae_relay_get_sink_uri (relay), ==, sink_uri);
  g_assert_cmpstr (hwangsae_relay_get_source_uri (relay), ==, source_uri);
}

typedef struct
{
  const gchar *source_uri;
  GaeguliVideoResolution resolution;
  gboolean done;
} RelayTestData;

static gboolean
validate_stream (RelayTestData * data)
{
  g_autoptr (GstDiscoverer) discoverer = NULL;
  g_autoptr (GstDiscovererInfo) info = NULL;
  g_autoptr (GstDiscovererStreamInfo) stream_info = NULL;
  g_autoptr (GstCaps) stream_caps = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *stream_caps_str = NULL;
  g_autolist (GstDiscovererStreamInfo) streams = NULL;
  GstDiscovererVideoInfo *video_info;
  guint expected_width;
  guint expected_height;

  discoverer = gst_discoverer_new (20 * GST_SECOND, &error);
  g_assert_no_error (error);

  info = gst_discoverer_discover_uri (discoverer, data->source_uri, &error);
  g_assert_no_error (error);
  g_assert_cmpint (gst_discoverer_info_get_result (info), ==,
      GST_DISCOVERER_OK);

  stream_info = gst_discoverer_info_get_stream_info (info);
  g_assert (GST_IS_DISCOVERER_CONTAINER_INFO (stream_info));

  stream_caps = gst_discoverer_stream_info_get_caps (stream_info);
  stream_caps_str = gst_caps_to_string (stream_caps);
  g_debug ("Stream has caps: %s", stream_caps_str);

  g_assert_cmpint (gst_caps_get_size (stream_caps), ==, 1);
  g_assert_cmpstr
      (gst_structure_get_name (gst_caps_get_structure (stream_caps, 0)), ==,
      "video/mpegts");

  streams =
      gst_discoverer_container_info_get_streams (GST_DISCOVERER_CONTAINER_INFO
      (stream_info));

  g_assert_cmpint (g_list_length (streams), ==, 1);

  g_assert (GST_IS_DISCOVERER_VIDEO_INFO (streams->data));
  video_info = streams->data;

  switch (data->resolution) {
    case GAEGULI_VIDEO_RESOLUTION_640X480:
      expected_width = 640;
      expected_height = 480;
      break;
    case GAEGULI_VIDEO_RESOLUTION_1920X1080:
      expected_width = 1920;
      expected_height = 1080;
      break;
    default:
      g_assert_not_reached ();
  }

  g_assert_cmpint (gst_discoverer_video_info_get_width (video_info), ==,
      expected_width);
  g_assert_cmpint (gst_discoverer_video_info_get_height (video_info), ==,
      expected_height);

  data->done = TRUE;

  return G_SOURCE_REMOVE;
}

static void
test_1_to_n (void)
{
  g_autoptr (HwangsaeTestStreamer) streamer = hwangsae_test_streamer_new ();
  g_autoptr (HwangsaeRelay) relay = hwangsae_relay_new (NULL, 8888, 9999);
  g_autofree gchar *source_uri = NULL;
  RelayTestData data1 = { 0 };
  RelayTestData data2 = { 0 };

  g_object_set (relay, "authentication", TRUE, NULL);

  source_uri = build_source_uri (streamer, relay, NULL);
  data1.source_uri = data2.source_uri = source_uri;
  data1.resolution = data2.resolution = GAEGULI_VIDEO_RESOLUTION_640X480;

  hwangsae_test_streamer_set_uri (streamer,
      hwangsae_relay_get_sink_uri (relay));
  hwangsae_test_streamer_start (streamer);

  /* Connect and validate two receivers. */
  g_idle_add ((GSourceFunc) validate_stream, &data1);
  g_idle_add ((GSourceFunc) validate_stream, &data2);

  while (!data1.done && !data2.done) {
    g_main_context_iteration (NULL, FALSE);
  }
}

static gchar *
build_source_uri (HwangsaeTestStreamer * streamer, HwangsaeRelay * relay,
    const gchar * username)
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

static void
test_m_to_n (void)
{
  g_autoptr (HwangsaeTestStreamer) streamer1 = hwangsae_test_streamer_new ();
  g_autoptr (HwangsaeTestStreamer) streamer2 = hwangsae_test_streamer_new ();
  g_autoptr (HwangsaeRelay) relay = hwangsae_relay_new (NULL, 8888, 9999);
  g_autofree gchar *source_uri1 = NULL;
  g_autofree gchar *source_uri2 = NULL;
  RelayTestData data1 = { 0 };
  RelayTestData data2 = { 0 };

  g_object_set (relay, "authentication", TRUE, NULL);

  hwangsae_test_streamer_set_uri (streamer1,
      hwangsae_relay_get_sink_uri (relay));
  data1.resolution = GAEGULI_VIDEO_RESOLUTION_640X480;
  g_object_set (streamer1, "resolution", data1.resolution, NULL);
  data1.source_uri = source_uri1 = build_source_uri (streamer1, relay, NULL);

  hwangsae_test_streamer_set_uri (streamer2,
      hwangsae_relay_get_sink_uri (relay));
  data2.resolution = GAEGULI_VIDEO_RESOLUTION_1920X1080;
  g_object_set (streamer2, "resolution", data2.resolution, NULL);
  data2.source_uri = source_uri2 = build_source_uri (streamer2, relay, NULL);

  hwangsae_test_streamer_start (streamer1);
  hwangsae_test_streamer_start (streamer2);

  g_idle_add ((GSourceFunc) validate_stream, &data1);
  g_idle_add ((GSourceFunc) validate_stream, &data2);

  while (!data1.done && !data2.done) {
    g_main_context_iteration (NULL, FALSE);
  }
}

static void
_on_sink_rejected (HwangsaeRelay * relay, HwangsaeCallerDirection direction,
    GInetSocketAddress * addr, const gchar * username, const gchar * resource,
    gpointer data)
{
  GInetAddress *ip = NULL;
  g_autofree gchar *ip_str = NULL;
  g_autofree gchar *local_ip_str = NULL;

  g_assert_cmpint (direction, ==, HWANGSAE_CALLER_DIRECTION_SINK);
  g_assert_null (username);

  ip = g_inet_socket_address_get_address (addr);
  ip_str = g_inet_address_to_string (ip);

  local_ip_str = hwangsae_common_get_local_ip ();

  g_assert_cmpstr (ip_str, ==, local_ip_str);

  g_main_loop_quit (data);
}

static void
test_reject_sink (void)
{
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr (HwangsaeTestStreamer) streamer = hwangsae_test_streamer_new ();
  g_autoptr (HwangsaeRelay) relay = hwangsae_relay_new (NULL, 8888, 9999);

  g_object_set (relay, "authentication", TRUE, NULL);

  hwangsae_test_streamer_set_uri (streamer,
      hwangsae_relay_get_sink_uri (relay));
  g_object_set (streamer, "username", NULL, NULL);

  g_signal_connect (relay, "caller-rejected", (GCallback) _on_sink_rejected,
      loop);

  hwangsae_test_streamer_start (streamer);

  g_main_loop_run (loop);
}

static void
_on_source_rejected (HwangsaeRelay * relay, HwangsaeCallerDirection direction,
    GInetSocketAddress * addr, const gchar * username, const gchar * resource,
    gpointer data)
{
  GInetAddress *ip = NULL;
  g_autofree gchar *ip_str = NULL;

  g_assert_cmpint (direction, ==, HWANGSAE_CALLER_DIRECTION_SRC);
  g_assert_null (resource);

  ip = g_inet_socket_address_get_address (addr);
  ip_str = g_inet_address_to_string (ip);

  g_assert_cmpstr (ip_str, ==, "127.0.0.1");

  g_main_loop_quit (data);
}

static void
test_reject_source (void)
{
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr (HwangsaeRelay) relay = hwangsae_relay_new (NULL, 8888, 9999);
  g_autoptr (GstElement) receiver = NULL;
  g_autoptr (GError) error = NULL;

  g_object_set (relay, "authentication", TRUE, NULL);

  g_signal_connect (relay, "caller-rejected", (GCallback) _on_source_rejected,
      loop);

  receiver = gst_parse_launch ("srtsrc uri=srt://127.0.0.1:9999?mode=caller ! "
      "fakesink", &error);
  g_assert_no_error (error);

  gst_element_set_state (receiver, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  gst_element_set_state (receiver, GST_STATE_NULL);
}

static const gchar *ACCEPTED_USERNAME = "ValidName";
static const gchar *REJECTED_USERNAME = "YouShallNotPass";

typedef struct
{
  gboolean sink_accepted;
  gboolean sink_rejected;
  gboolean source_accepted;
  gboolean source_rejected;
} AuthenticationTestData;

static gboolean
_authenticate (HwangsaeRelay * relay, HwangsaeCallerDirection direction,
    GSocketAddress * addr, const gchar * username, const gchar * resource,
    gpointer data)
{
  return g_strcmp0 (username, REJECTED_USERNAME);
}

static void
_caller_accepted (HwangsaeRelay * relay, HwangsaeCallerDirection direction,
    GSocketAddress * addr, const gchar * username, const gchar * resource,
    AuthenticationTestData * data)
{
  if (!g_strcmp0 (username, ACCEPTED_USERNAME)) {
    switch (direction) {
      case HWANGSAE_CALLER_DIRECTION_SINK:
        data->sink_accepted = TRUE;
        break;
      case HWANGSAE_CALLER_DIRECTION_SRC:
        data->source_accepted = TRUE;
        break;
    }

    return;
  }

  g_assert_not_reached ();
}

static void
_caller_rejected (HwangsaeRelay * relay, HwangsaeCallerDirection direction,
    GSocketAddress * addr, const gchar * username, const gchar * resource,
    AuthenticationTestData * data)
{
  if (!g_strcmp0 (username, REJECTED_USERNAME)) {
    switch (direction) {
      case HWANGSAE_CALLER_DIRECTION_SINK:
        data->sink_rejected = TRUE;
        break;
      case HWANGSAE_CALLER_DIRECTION_SRC:
        data->source_rejected = TRUE;
        break;
    }

    return;
  }

  g_assert_not_reached ();
}

static void
test_authentication (void)
{
  AuthenticationTestData data = { 0 };
  g_autoptr (HwangsaeRelay) relay = hwangsae_relay_new (NULL, 8888, 9999);
  g_autoptr (HwangsaeTestStreamer) stream = hwangsae_test_streamer_new ();
  g_autofree gchar *uri = NULL;
  g_autofree gchar *pipeline = NULL;
  g_autoptr (GstElement) receiver = NULL;
  g_autoptr (GError) error = NULL;

  g_object_set (relay, "authentication", TRUE, NULL);
  g_object_set (stream, "username", REJECTED_USERNAME, NULL);

  g_signal_connect (relay, "caller-accepted", (GCallback) _caller_accepted,
      &data);
  g_signal_connect (relay, "caller-rejected", (GCallback) _caller_rejected,
      &data);
  g_signal_connect (relay, "authenticate", (GCallback) _authenticate, &data);

  hwangsae_test_streamer_set_uri (stream, hwangsae_relay_get_sink_uri (relay));
  hwangsae_test_streamer_start (stream);

  while (!data.sink_rejected) {
    g_main_context_iteration (NULL, FALSE);
  }

  hwangsae_test_streamer_stop (stream);
  g_object_set (stream, "username", ACCEPTED_USERNAME, NULL);
  hwangsae_test_streamer_start (stream);

  while (!data.sink_accepted) {
    g_main_context_iteration (NULL, FALSE);
  }

  uri = build_source_uri (stream, relay, REJECTED_USERNAME);
  pipeline = g_strdup_printf ("srtsrc uri=%s ! fakesink", uri);
  receiver = gst_parse_launch (pipeline, &error);
  g_clear_pointer (&uri, g_free);
  g_clear_pointer (&pipeline, g_free);
  g_assert_no_error (error);

  gst_element_set_state (receiver, GST_STATE_PLAYING);

  while (!data.source_rejected) {
    g_main_context_iteration (NULL, FALSE);
  }

  gst_element_set_state (receiver, GST_STATE_NULL);
  gst_clear_object (&receiver);

  uri = build_source_uri (stream, relay, ACCEPTED_USERNAME);
  pipeline = g_strdup_printf ("srtsrc uri=%s ! fakesink", uri);
  receiver = gst_parse_launch (pipeline, &error);
  g_assert_no_error (error);

  gst_element_set_state (receiver, GST_STATE_PLAYING);

  while (!data.source_accepted) {
    g_main_context_iteration (NULL, FALSE);
  }

  gst_element_set_state (receiver, GST_STATE_NULL);
}

static void
_flip_flag (HwangsaeRelay * relay, HwangsaeCallerDirection direction,
    GInetSocketAddress * addr, const gchar * username, const gchar * resource,
    gpointer data)
{
  *(gboolean *) data = TRUE;
}

static void
test_no_auth (void)
{
  g_autoptr (HwangsaeRelay) relay = hwangsae_relay_new (NULL, 8888, 9999);
  g_autoptr (HwangsaeTestStreamer) stream1 = hwangsae_test_streamer_new ();
  g_autoptr (HwangsaeTestStreamer) stream2 = hwangsae_test_streamer_new ();
  g_autofree gchar *source_uri = NULL;
  gboolean stream1_accepted = FALSE;
  gboolean stream2_rejected = FALSE;
  RelayTestData data1 = { 0 };
  RelayTestData data2 = { 0 };

  data1.resolution = data2.resolution = GAEGULI_VIDEO_RESOLUTION_640X480;
  data1.source_uri = data2.source_uri = source_uri =
      build_source_uri (stream1, relay, NULL);

  g_object_set (relay, "authentication", FALSE, NULL);

  /* Connect first streamer. */

  g_signal_connect (relay, "caller-accepted", (GCallback) _flip_flag,
      &stream1_accepted);

  g_object_set (stream1, "resolution", data1.resolution, NULL);
  hwangsae_test_streamer_set_uri (stream1, hwangsae_relay_get_sink_uri (relay));
  hwangsae_test_streamer_start (stream1);

  while (!stream1_accepted) {
    g_main_context_iteration (NULL, FALSE);
  }

  g_debug ("stream1 accepted");

  /* Try connecting second stream. In unauthenticated mode this should fail. */

  g_signal_connect (relay, "caller-rejected", (GCallback) _flip_flag,
      &stream2_rejected);

  hwangsae_test_streamer_set_uri (stream2, hwangsae_relay_get_sink_uri (relay));
  hwangsae_test_streamer_start (stream2);

  while (!stream2_rejected) {
    g_main_context_iteration (NULL, FALSE);
  }

  g_debug ("stream2 rejected");

  /* Check that two sources can be connected to the first (and only ) sink. */

  g_idle_add ((GSourceFunc) validate_stream, &data1);
  g_idle_add ((GSourceFunc) validate_stream, &data2);

  while (!data1.done && !data2.done) {
    g_main_context_iteration (NULL, FALSE);
  }

  g_debug ("Receiving stream1 validated");
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  /* Don't treat warnings as fatal, which is GTest default. */
  g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);

  g_test_add_func ("/hwangsae/relay-instance", test_relay_instance);
  g_test_add_func ("/hwangsae/relay-1-to-n", test_1_to_n);
  g_test_add_func ("/hwangsae/relay-m-to-n", test_m_to_n);
  g_test_add_func ("/hwangsae/relay-external-ip", test_external_ip);
  g_test_add_func ("/hwangsae/relay-reject-sink", test_reject_sink);
  g_test_add_func ("/hwangsae/relay-reject-source", test_reject_source);
  g_test_add_func ("/hwangsae/relay-authentication", test_authentication);
  g_test_add_func ("/hwangsae/relay-no-auth", test_no_auth);

  return g_test_run ();
}
