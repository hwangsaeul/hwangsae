/**
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

#include "transmuxer.h"

#include "common.h"

#include <gst/gst.h>

struct _HwangsaeTransmuxer
{
  GObject parent;
};

typedef struct
{
  GMutex lock;

  GstElement *pipeline;
  GstElement *concat;

  GList *segments;
  guint current_segment;

  gboolean have_eos;
} HwangsaeTransmuxerPrivate;

/* *INDENT-OFF* */
G_DEFINE_TYPE_WITH_PRIVATE (HwangsaeTransmuxer, hwangsae_transmuxer, G_TYPE_OBJECT)
/* *INDENT-ON* */

typedef struct
{
  guint64 base_time;
  GstElement *parsebin;
  gchar *filename;
} Segment;

static void
_free_segment (Segment * segment)
{
  g_clear_pointer (&segment->filename, g_free);
  g_free (segment);
}

static gint
_compare_segments (gconstpointer a, gconstpointer b)
{
  gint64 diff = ((Segment *) a)->base_time - ((Segment *) b)->base_time;

  return (diff < 0) ? -1 : ((diff > 0) ? 1 : 0);
}

HwangsaeTransmuxer *
hwangsae_transmuxer_new (void)
{
  return g_object_new (HWANGSAE_TYPE_TRANSMUXER, NULL);
}

static void
hwangsae_transmuxer_init (HwangsaeTransmuxer * self)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (self);

  static gsize gst_initialized = 0;

  if (g_once_init_enter (&gst_initialized)) {
    gst_init (NULL, NULL);
    g_once_init_leave (&gst_initialized, 1);
  }

  g_mutex_init (&priv->lock);
}

static gboolean
_bus_watch (GstBus * bus, GstMessage * message, gpointer user_data)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (user_data);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      priv->have_eos = TRUE;
      break;
    default:
      break;
  }

  return G_SOURCE_CONTINUE;
}

static GstPadProbeReturn
_src_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (user_data);

  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

  if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
    Segment *s;
    GstSegment *segment = gst_segment_new ();

    gst_event_copy_segment (event, segment);

    s = g_list_nth_data (priv->segments, priv->current_segment++);

    segment->base = s->base_time;

    gst_event_unref (event);
    GST_PAD_PROBE_INFO_DATA (info) = gst_event_new_segment (segment);
  }

  return GST_PAD_PROBE_OK;
}

static gint
_find_segment_by_parsebin (gconstpointer a, gconstpointer b)
{
  return ((Segment *) a)->parsebin != b;
}

static void _pad_added (GstElement * element, GstPad * pad, gpointer user_data);

static void
hwangsae_transmuxer_link_segment (HwangsaeTransmuxer * self, Segment * segment)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (self);

  GstElement *filesrc;

  filesrc = gst_element_factory_make ("filesrc", NULL);
  g_object_set (filesrc, "location", segment->filename, NULL);

  segment->parsebin = gst_element_factory_make ("parsebin", NULL);

  gst_bin_add_many (GST_BIN (priv->pipeline), filesrc, segment->parsebin, NULL);
  gst_element_link (filesrc, segment->parsebin);

  gst_element_sync_state_with_parent (filesrc);
  gst_element_sync_state_with_parent (segment->parsebin);

  g_signal_connect (segment->parsebin, "pad-added", G_CALLBACK (_pad_added),
      self);
}

static void
_pad_added (GstElement * element, GstPad * pad, gpointer user_data)
{
  HwangsaeTransmuxer *self = user_data;
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (user_data);

  Segment *segment;
  GList *item;

  g_mutex_lock (&priv->lock);

  item = g_list_find_custom (priv->segments, element,
      _find_segment_by_parsebin);
  segment = item->data;

  gst_element_link (segment->parsebin, priv->concat);

  if (item->next) {
    hwangsae_transmuxer_link_segment (self, (Segment *) item->next->data);
  } else {
    gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
  }

  g_mutex_unlock (&priv->lock);
}

static void
hwangsae_transmuxer_clear (HwangsaeTransmuxer * self)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (self);

  if (priv->pipeline) {
    gst_element_set_state (priv->pipeline, GST_STATE_NULL);
  }

  g_list_free_full (priv->segments, (GDestroyNotify) _free_segment);
  priv->segments = NULL;
  priv->current_segment = 0;
  priv->have_eos = FALSE;
  gst_clear_object (&priv->concat);
  gst_clear_object (&priv->pipeline);
}

static void
hwangsae_transmuxer_parse_segments (HwangsaeTransmuxer * self,
    GSList * input_files)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (self);

  GList *segments = NULL;
  GList *it;

  for (; input_files; input_files = input_files->next) {
    gchar *file = input_files->data;
    Segment *segment;
    guint64 segment_start;

    if (!hwangsae_common_parse_times_from_filename (file, &segment_start, NULL)) {
      g_warning ("Invalid filename %s", file);
      continue;
    }

    segment = g_new0 (Segment, 1);
    segment->filename = g_strdup (file);
    segment->base_time = segment_start;

    segments = g_list_insert_sorted (segments, segment, _compare_segments);
  }

  /* Make base times start from zero. */
  for (it = g_list_last (segments); it; it = it->prev) {
    ((Segment *) it->data)->base_time -=
        ((Segment *) segments->data)->base_time;
  }

  priv->segments = segments;
}

void
hwangsae_transmuxer_merge (HwangsaeTransmuxer * self, GSList * input_files,
    const gchar * output, GError ** error)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (self);

  g_autoptr (GstElement) sink = NULL;
  g_autoptr (GstPad) pad = NULL;
  g_autoptr (GstBus) bus = NULL;
  g_autoptr (GError) parse_error = NULL;

  g_return_if_fail (input_files != NULL);
  g_return_if_fail (output != NULL);

  priv->pipeline = gst_parse_launch ("concat name=concat ! h264parse ! mp4mux "
      "! filesink name=sink", &parse_error);

  if (parse_error) {
    g_propagate_error (error, parse_error);
    return;
  }

  bus = gst_element_get_bus (priv->pipeline);
  gst_bus_add_watch (bus, _bus_watch, self);

  priv->concat = gst_bin_get_by_name (GST_BIN (priv->pipeline), "concat");
  sink = gst_bin_get_by_name (GST_BIN (priv->pipeline), "sink");

  g_object_set (priv->concat, "adjust-base", FALSE, NULL);
  g_object_set (sink, "location", output, NULL);

  pad = gst_element_get_static_pad (priv->concat, "src");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, _src_probe, self,
      NULL);

  hwangsae_transmuxer_parse_segments (self, input_files);

  hwangsae_transmuxer_link_segment (self, g_list_nth_data (priv->segments, 0));

  gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);

  while (!priv->have_eos) {
    g_main_context_iteration (NULL, TRUE);
  }

  hwangsae_transmuxer_clear (self);
}

static void
hwangsae_transmuxer_finalize (GObject * object)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (HWANGSAE_TRANSMUXER (object));

  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (hwangsae_transmuxer_parent_class)->finalize (object);
}

static void
hwangsae_transmuxer_class_init (HwangsaeTransmuxerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = hwangsae_transmuxer_finalize;
}
