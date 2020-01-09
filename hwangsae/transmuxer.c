/**
 *  Copyright 2019-2020 SK Telecom Co., Ltd.
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

#include "types.h"

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
  GstElement *splitmux;

  GList *segments;
  GList *current_segment_link;

  guint64 max_size_time;
  guint64 max_size_bytes;
  gboolean have_split_at_running_time;

  gboolean have_eos;
} HwangsaeTransmuxerPrivate;

/* *INDENT-OFF* */
G_DEFINE_TYPE_WITH_PRIVATE (HwangsaeTransmuxer, hwangsae_transmuxer, G_TYPE_OBJECT)
/* *INDENT-ON* */

typedef struct
{
  guint64 base_time;
  guint64 end_time;
  GstElement *parsebin;
  gchar *filename;
} Segment;

enum
{
  PROP_MAX_SIZE_TIME = 1,
  PROP_MAX_SIZE_BYTES,
  PROP_LAST
};

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

  priv->splitmux =
      gst_object_ref_sink (gst_element_factory_make ("splitmuxsink", NULL));
  g_object_set (priv->splitmux, "async-finalize", TRUE,
      "muxer-factory", "mp4mux", NULL);
}

void
hwangsae_transmuxer_set_max_size_time (HwangsaeTransmuxer * self,
    guint64 duration_ns)
{
  g_object_set (self, "max-size-time", duration_ns, NULL);
}

guint64
hwangsae_transmuxer_get_max_size_time (HwangsaeTransmuxer * self)
{
  guint64 result;

  g_object_get (self, "max-size-time", &result, NULL);

  return result;
}

void
hwangsae_transmuxer_set_max_size_bytes (HwangsaeTransmuxer * self, guint64 size)
{
  g_object_set (self, "max-size-bytes", size, NULL);
}

guint64
hwangsae_transmuxer_get_max_size_bytes (HwangsaeTransmuxer * self)
{
  guint64 result;

  g_object_get (self, "max-size-bytes", &result, NULL);

  return result;
}


static gint _find_segment_by_parsebin (gconstpointer a, gconstpointer b);
static void hwangsae_transmuxer_link_next_segment (HwangsaeTransmuxer * self);

static gboolean
_bus_watch (GstBus * bus, GstMessage * message, gpointer user_data)
{
  HwangsaeTransmuxer *self = user_data;
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (self);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      priv->have_eos = TRUE;
      break;
    case GST_MESSAGE_ERROR:{
      g_autoptr (GError) err = NULL;
      g_autofree gchar *dbg_info = NULL;

      gst_message_parse_error (message, &err, &dbg_info);
      g_warning ("Domain: %s, code: %d, message: %s",
          g_quark_to_string (err->domain), err->code, err->message);

      if (err->domain == GST_STREAM_ERROR
          && err->code == GST_STREAM_ERROR_FAILED) {
        Segment *segment = priv->current_segment_link->data;

        g_warning ("Error while processing %s. The file is likely corrupted.",
            segment->filename);

        hwangsae_transmuxer_link_next_segment (self);
      }
      break;
    }
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
    g_autoptr (GstPad) active_pad = NULL;
    GList *item;
    GstSegment *segment;

    g_object_get (GST_PAD_PARENT (pad), "active-pad", &active_pad, NULL);

    item = g_list_find_custom (priv->segments,
        GST_PAD_PARENT (GST_PAD_PEER (active_pad)), _find_segment_by_parsebin);

    if (!item) {
      g_warning ("No segment for active pad %s!", GST_PAD_NAME (active_pad));
      goto out;
    }

    segment = gst_segment_new ();
    gst_event_copy_segment (event, segment);
    segment->base = ((Segment *) item->data)->base_time;

    g_debug ("New segment with base %" GST_TIME_FORMAT,
        GST_TIME_ARGS (segment->base));

    gst_event_unref (event);
    GST_PAD_PROBE_INFO_DATA (info) = gst_event_new_segment (segment);
  }

out:
  return GST_PAD_PROBE_OK;
}

static gint
_find_segment_by_parsebin (gconstpointer a, gconstpointer b)
{
  return ((Segment *) a)->parsebin != b;
}

static void _pad_added (GstElement * element, GstPad * pad, gpointer user_data);

static void
hwangsae_transmuxer_link_next_segment (HwangsaeTransmuxer * self)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (self);

  if (!priv->current_segment_link) {
    /* We're linking the first segment. */
    priv->current_segment_link = priv->segments;
  } else {
    priv->current_segment_link = priv->current_segment_link->next;
  }

  if (priv->current_segment_link) {
    Segment *segment;
    GstElement *filesrc;

    segment = priv->current_segment_link->data;

    filesrc = gst_element_factory_make ("filesrc", NULL);
    g_object_set (filesrc, "location", segment->filename, NULL);

    segment->parsebin = gst_element_factory_make ("parsebin", NULL);

    gst_bin_add_many (GST_BIN (priv->pipeline), filesrc, segment->parsebin,
        NULL);
    gst_element_link (filesrc, segment->parsebin);

    gst_element_sync_state_with_parent (filesrc);
    gst_element_sync_state_with_parent (segment->parsebin);

    g_signal_connect (segment->parsebin, "pad-added", G_CALLBACK (_pad_added),
        self);
  } else {
    /* All segments have been linked. Start the pipeline. */
    gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
  }
}

static void
_pad_added (GstElement * element, GstPad * pad, gpointer user_data)
{
  HwangsaeTransmuxer *self = user_data;
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (user_data);

  Segment *segment;

  g_mutex_lock (&priv->lock);

  segment = priv->current_segment_link->data;

  g_assert (segment->parsebin == element);

  gst_element_link (segment->parsebin, priv->concat);

  hwangsae_transmuxer_link_next_segment (self);

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
  priv->current_segment_link = NULL;
  priv->have_eos = FALSE;
  priv->have_split_at_running_time = FALSE;
  gst_clear_object (&priv->concat);
  gst_clear_object (&priv->pipeline);
}

static GSList *
_check_input_files (GSList * input_files)
{
  for (GSList * it = input_files; it;) {
    GSList *next = g_slist_next (it);
    gchar *file = it->data;
    if (!g_file_test (file, G_FILE_TEST_IS_REGULAR)) {
      g_warning ("File %s not found, ominting it", file);
      input_files = g_slist_delete_link (input_files, it);
    }
    it = next;
  }

  return input_files;
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
    guint64 segment_start, segment_end;

    if (!hwangsae_common_parse_times_from_filename (file, &segment_start,
            &segment_end)) {
      g_warning ("Invalid filename %s", file);
      continue;
    }

    segment = g_new0 (Segment, 1);
    segment->filename = g_strdup (file);
    segment->base_time = segment_start;
    segment->end_time = segment_end;

    segments = g_list_insert_sorted (segments, segment, _compare_segments);
  }

  /* Make base times start from zero. */
  for (it = g_list_last (segments); it; it = it->prev) {
    ((Segment *) it->data)->end_time -= ((Segment *) segments->data)->base_time;
    ((Segment *) it->data)->base_time -=
        ((Segment *) segments->data)->base_time;
  }

  priv->segments = segments;
}

static gboolean
_check_overlap (HwangsaeTransmuxer * self)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (self);
  GList *it;

  if (!priv->segments) {
    /* Empty segment list. */
    return TRUE;
  }

  for (it = priv->segments->next; it; it = it->next) {
    Segment *segment = it->data;
    Segment *previous_segment = it->prev->data;

    if (previous_segment->end_time > segment->base_time)
      return FALSE;
  }
  return TRUE;
}

void
hwangsae_transmuxer_merge (HwangsaeTransmuxer * self, GSList * input_files,
    const gchar * output, GError ** error)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (self);

  g_autoptr (GstPad) pad = NULL;
  g_autoptr (GstBus) bus = NULL;
  g_autoptr (GError) parse_error = NULL;
  g_autofree gchar *output_tmp = NULL;
  GstElement *parse = NULL;

  g_return_if_fail (input_files != NULL);
  g_return_if_fail (output != NULL);

  input_files = _check_input_files (input_files);

  hwangsae_transmuxer_parse_segments (self, input_files);

  if (!_check_overlap (self)) {
    g_warning ("There are overlapping segments");
    g_set_error (error, HWANGSAE_TRANSMUXER_ERROR,
        HWANGSAE_TRANSMUXER_ERROR_OVERLAP, "Overlapping segments");
    return;
  }

  priv->pipeline = gst_pipeline_new (NULL);
  priv->concat =
      gst_object_ref_sink (gst_element_factory_make ("concat", NULL));
  parse = gst_element_factory_make ("h264parse", NULL);

  gst_bin_add_many (GST_BIN (priv->pipeline), priv->concat, parse,
      priv->splitmux, NULL);
  gst_element_link_many (priv->concat, parse, priv->splitmux, NULL);

  bus = gst_element_get_bus (priv->pipeline);
  gst_bus_add_watch (bus, _bus_watch, self);

  g_object_set (priv->concat, "adjust-base", FALSE, NULL);

  if ((priv->max_size_time != 0 || priv->max_size_bytes != 0 ||
          priv->have_split_at_running_time) && (strstr (output, "%") == NULL)) {
    /* Output filename is not a pattern, add fragment number as a suffix. */
    output_tmp = g_strconcat (output, ".%05d", NULL);
    output = output_tmp;
  }
  g_object_set (priv->splitmux, "location", output, NULL);

  g_object_set (priv->splitmux, "max-size-time", priv->max_size_time,
      "max-size-bytes", priv->max_size_bytes, NULL);

  pad = gst_element_get_static_pad (priv->concat, "src");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, _src_probe, self,
      NULL);

  hwangsae_transmuxer_link_next_segment (self);

  gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);

  while (!priv->have_eos) {
    g_main_context_iteration (NULL, TRUE);
  }

  hwangsae_transmuxer_clear (self);
}

void
hwangsae_transmuxer_split_at_running_time (HwangsaeTransmuxer * self,
    GstClockTime time)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (self);

  g_signal_emit_by_name (priv->splitmux, "split-at-running-time", time);
  priv->have_split_at_running_time = TRUE;
}

static void
hwangsae_transmuxer_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (HWANGSAE_TRANSMUXER (object));

  switch (property_id) {
    case PROP_MAX_SIZE_TIME:
      priv->max_size_time = g_value_get_uint64 (value);
      break;
    case PROP_MAX_SIZE_BYTES:
      priv->max_size_bytes = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
hwangsae_transmuxer_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (HWANGSAE_TRANSMUXER (object));

  switch (property_id) {
    case PROP_MAX_SIZE_TIME:
      g_value_set_uint64 (value, priv->max_size_time);
      break;
    case PROP_MAX_SIZE_BYTES:
      g_value_set_uint64 (value, priv->max_size_bytes);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
hwangsae_transmuxer_finalize (GObject * object)
{
  HwangsaeTransmuxerPrivate *priv =
      hwangsae_transmuxer_get_instance_private (HWANGSAE_TRANSMUXER (object));

  gst_clear_object (&priv->splitmux);

  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (hwangsae_transmuxer_parent_class)->finalize (object);
}

static void
hwangsae_transmuxer_class_init (HwangsaeTransmuxerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = hwangsae_transmuxer_set_property;
  gobject_class->get_property = hwangsae_transmuxer_get_property;

  gobject_class->finalize = hwangsae_transmuxer_finalize;

  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_TIME,
      g_param_spec_uint64 ("max-size-time", "Max recording file duration",
          "Max amount of time per file (in ns, 0 = disable)",
          0, G_MAXUINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_BYTES,
      g_param_spec_uint64 ("max-size-bytes", "Max recording file size in bytes",
          "Max amount of bytes per file (0 = disable)",
          0, G_MAXUINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}
