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
#include "hwangsae/test/test.h"

#include <gaeguli/gaeguli.h>
#include <gio/gio.h>
#include <gst/pbutils/gstdiscoverer.h>

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

  source_uri = hwangsae_test_build_source_uri (streamer, relay, NULL);
  data1.source_uri = data2.source_uri = source_uri;
  data1.resolution = data2.resolution = GAEGULI_VIDEO_RESOLUTION_640X480;

  hwangsae_test_streamer_set_uri (streamer,
      hwangsae_relay_get_sink_uri (relay));

  hwangsae_relay_start (relay);
  hwangsae_test_streamer_start (streamer);

  /* Connect and validate two receivers. */
  g_idle_add ((GSourceFunc) validate_stream, &data1);
  g_idle_add ((GSourceFunc) validate_stream, &data2);

  while (!data1.done && !data2.done) {
    g_main_context_iteration (NULL, FALSE);
  }
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
  data1.source_uri = source_uri1 =
      hwangsae_test_build_source_uri (streamer1, relay, NULL);

  hwangsae_test_streamer_set_uri (streamer2,
      hwangsae_relay_get_sink_uri (relay));
  data2.resolution = GAEGULI_VIDEO_RESOLUTION_1920X1080;
  g_object_set (streamer2, "resolution", data2.resolution, NULL);
  data2.source_uri = source_uri2 =
      hwangsae_test_build_source_uri (streamer2, relay, NULL);

  hwangsae_relay_start (relay);
  hwangsae_test_streamer_start (streamer1);
  hwangsae_test_streamer_start (streamer2);

  g_idle_add ((GSourceFunc) validate_stream, &data1);
  g_idle_add ((GSourceFunc) validate_stream, &data2);

  while (!data1.done && !data2.done) {
    g_main_context_iteration (NULL, FALSE);
  }
}

static void
_on_sink_rejected (HwangsaeRelay * relay, gint id,
    HwangsaeCallerDirection direction, GInetSocketAddress * addr,
    const gchar * username, const gchar * resource, gpointer data)
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

  hwangsae_relay_start (relay);
  hwangsae_test_streamer_start (streamer);

  g_main_loop_run (loop);
}

static void
_on_source_rejected (HwangsaeRelay * relay, int id,
    HwangsaeCallerDirection direction, GInetSocketAddress * addr,
    const gchar * username, const gchar * resource, gpointer data)
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

  hwangsae_relay_start (relay);

  receiver = gst_parse_launch ("srtsrc uri=srt://127.0.0.1:9999?mode=caller ! "
      "fakesink", &error);
  g_assert_no_error (error);

  gst_element_set_state (receiver, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  gst_element_set_state (receiver, GST_STATE_NULL);
}

static const gchar *ACCEPTED_SINK = "AcceptedSink";
static const gchar *REJECTED_SINK = "RejectedSink";
static const gchar *ACCEPTED_SRC = "AcceptedSrc";
static const gchar *REJECTED_SRC = "RejectedSrc";

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
  switch (direction) {
    case HWANGSAE_CALLER_DIRECTION_SINK:
      return g_strcmp0 (username, REJECTED_SINK);
      break;
    case HWANGSAE_CALLER_DIRECTION_SRC:
      return g_strcmp0 (username, REJECTED_SRC);
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
_caller_accepted (HwangsaeRelay * relay, gint id,
    HwangsaeCallerDirection direction, GSocketAddress * addr,
    const gchar * username, const gchar * resource,
    AuthenticationTestData * data)
{
  switch (direction) {
    case HWANGSAE_CALLER_DIRECTION_SINK:
      data->sink_accepted = !g_strcmp0 (username, ACCEPTED_SINK);
      break;
    case HWANGSAE_CALLER_DIRECTION_SRC:
      data->source_accepted = !g_strcmp0 (username, ACCEPTED_SRC);
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
_caller_rejected (HwangsaeRelay * relay, gint id,
    HwangsaeCallerDirection direction, GSocketAddress * addr,
    const gchar * username, const gchar * resource,
    AuthenticationTestData * data)
{
  switch (direction) {
    case HWANGSAE_CALLER_DIRECTION_SINK:
      data->sink_rejected = !g_strcmp0 (username, REJECTED_SINK);
      break;
    case HWANGSAE_CALLER_DIRECTION_SRC:
      data->source_rejected = !g_strcmp0 (username, REJECTED_SRC);
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
test_authentication (void)
{
  AuthenticationTestData data = { 0 };
  g_autoptr (HwangsaeRelay) relay = hwangsae_relay_new (NULL, 8888, 9999);
  g_autoptr (HwangsaeTestStreamer) stream = hwangsae_test_streamer_new ();
  g_autoptr (GstElement) receiver = NULL;
  g_autoptr (GError) error = NULL;

  g_object_set (relay, "authentication", TRUE, NULL);
  g_object_set (stream, "username", REJECTED_SINK, NULL);

  g_signal_connect (relay, "caller-accepted", (GCallback) _caller_accepted,
      &data);
  g_signal_connect (relay, "caller-rejected", (GCallback) _caller_rejected,
      &data);
  g_signal_connect (relay, "authenticate", (GCallback) _authenticate, &data);

  hwangsae_test_streamer_set_uri (stream, hwangsae_relay_get_sink_uri (relay));

  hwangsae_relay_start (relay);
  hwangsae_test_streamer_start (stream);

  while (!data.sink_rejected) {
    g_main_context_iteration (NULL, FALSE);
  }

  hwangsae_test_streamer_stop (stream);
  g_object_set (stream, "username", ACCEPTED_SINK, NULL);
  hwangsae_test_streamer_start (stream);

  while (!data.sink_accepted) {
    g_main_context_iteration (NULL, FALSE);
  }

  receiver = hwangsae_test_make_receiver (stream, relay, REJECTED_SRC);

  while (!data.source_rejected) {
    g_main_context_iteration (NULL, FALSE);
  }

  gst_element_set_state (receiver, GST_STATE_NULL);
  gst_clear_object (&receiver);

  receiver = hwangsae_test_make_receiver (stream, relay, ACCEPTED_SRC);

  while (!data.source_accepted) {
    g_main_context_iteration (NULL, FALSE);
  }

  gst_element_set_state (receiver, GST_STATE_NULL);
}

static void
_flip_flag (HwangsaeRelay * relay, gint id, HwangsaeCallerDirection direction,
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
      hwangsae_test_build_source_uri (stream1, relay, NULL);

  g_object_set (relay, "authentication", FALSE, NULL);

  hwangsae_relay_start (relay);

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

  while (!data1.done || !data2.done) {
    g_main_context_iteration (NULL, FALSE);
  }

  g_debug ("Receiving stream1 validated");
}

#define MASTER_STREAM_RESOURCE "MyStream"
#define RECEIVER_USERNAME "MyReceiver"

typedef struct
{
  gboolean sink_accepted;
  gboolean slave_accepted;
  gboolean slave_rejected;
  gboolean receiver_rejected;
} SlaveTestData;

static void
_sink_accepted (HwangsaeRelay * relay, gint id,
    HwangsaeCallerDirection direction, GInetSocketAddress * addr,
    const gchar * username, const gchar * resource, SlaveTestData * data)
{
  if (direction == HWANGSAE_CALLER_DIRECTION_SINK) {
    g_assert_cmpstr (username, ==, MASTER_STREAM_RESOURCE);
    g_assert_cmpstr (resource, ==, NULL);

    g_debug ("Sink accepted");

    data->sink_accepted = TRUE;
  }
}

static void
_slave_accepted (HwangsaeRelay * relay, gint id,
    HwangsaeCallerDirection direction, GInetSocketAddress * addr,
    const gchar * username, const gchar * resource, SlaveTestData * data)
{
  if (direction == HWANGSAE_CALLER_DIRECTION_SRC) {
    g_debug ("Slave accepted");

    g_assert_cmpstr (username, ==, ACCEPTED_SRC);
    g_assert_cmpstr (resource, ==, MASTER_STREAM_RESOURCE);

    data->slave_accepted = TRUE;
  }
}

static void
_slave_rejected (HwangsaeRelay * relay, gint id,
    HwangsaeCallerDirection direction, GInetSocketAddress * addr,
    const gchar * username, const gchar * resource, SlaveTestData * data)
{
  if (direction == HWANGSAE_CALLER_DIRECTION_SRC) {
    g_debug ("Slave rejected");

    g_assert_cmpstr (username, ==, REJECTED_SRC);
    g_assert_cmpstr (resource, ==, MASTER_STREAM_RESOURCE);

    data->slave_rejected = TRUE;
  }
}

static void
_receiver_rejected (HwangsaeRelay * relay, gint id,
    HwangsaeCallerDirection direction, GInetSocketAddress * addr,
    const gchar * username, const gchar * resource, SlaveTestData * data)
{
  if (direction == HWANGSAE_CALLER_DIRECTION_SRC) {
    g_debug ("Receiver rejected");

    g_assert_cmpstr (username, ==, RECEIVER_USERNAME);
    g_assert_cmpstr (resource, ==, MASTER_STREAM_RESOURCE);

    data->receiver_rejected = TRUE;
  }
}

static void
test_slave (void)
{
  g_autoptr (HwangsaeRelay) master = hwangsae_relay_new (NULL, 8888, 9999);
  g_autoptr (HwangsaeRelay) slave = hwangsae_relay_new (NULL, 18888, 19999);
  g_autoptr (HwangsaeTestStreamer) stream = hwangsae_test_streamer_new ();
  g_autoptr (GstElement) receiver = NULL;
  SlaveTestData data = { 0 };
  RelayTestData validate_stream_data = { 0 };

  g_object_set (stream, "username", MASTER_STREAM_RESOURCE,
      "resolution", GAEGULI_VIDEO_RESOLUTION_640X480, NULL);

  g_object_set (master, "authentication", TRUE, NULL);
  g_signal_connect (master, "authenticate", (GCallback) _authenticate, NULL);
  g_signal_connect (master, "caller-accepted", (GCallback) _sink_accepted,
      &data);
  g_signal_connect (master, "caller-rejected", (GCallback) _slave_rejected,
      &data);
  g_signal_connect (slave, "caller-rejected", (GCallback) _receiver_rejected,
      &data);

  g_object_set (slave, "authentication", TRUE,
      "master-uri", hwangsae_relay_get_source_uri (master),
      "master-username", REJECTED_SRC, NULL);

  hwangsae_relay_start (master);
  hwangsae_relay_start (slave);

  hwangsae_test_streamer_set_uri (stream, hwangsae_relay_get_sink_uri (master));
  hwangsae_test_streamer_start (stream);

  /* Wait until the streamer connects to the master relay as a sink. */
  while (!data.sink_accepted) {
    g_main_context_iteration (NULL, FALSE);
  }

  receiver = hwangsae_test_make_receiver (stream, slave, RECEIVER_USERNAME);

  /* Slave relay should get rejected by the master due to its username. */
  while (!data.receiver_rejected || !data.slave_rejected) {
    g_main_context_iteration (NULL, FALSE);
  }

  /* Switch slave's username to the accepted one. Now we should be able to
   * establish full path stream -> master <- slave <- receiver */
  g_object_set (slave, "master-username", ACCEPTED_SRC, NULL);

  g_signal_connect (master, "caller-accepted", (GCallback) _slave_accepted,
      &data);

  while (!data.slave_accepted) {
    g_main_context_iteration (NULL, FALSE);
  }

  validate_stream_data.source_uri =
      hwangsae_test_build_source_uri (stream, slave, RECEIVER_USERNAME);
  validate_stream_data.resolution = GAEGULI_VIDEO_RESOLUTION_640X480;
  g_idle_add ((GSourceFunc) validate_stream, &validate_stream_data);

  while (!validate_stream_data.done) {
    g_main_context_iteration (NULL, FALSE);
  }

  gst_element_set_state (receiver, GST_STATE_NULL);
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
  g_test_add_func ("/hwangsae/relay-slave", test_slave);

  return g_test_run ();
}
