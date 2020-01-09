/**
 *  Copyright 2019-2020 SK Telecom, Co., Ltd.
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
 */

#include "hwangsae/transmuxer.h"
#include "hwangsae/types.h"

#include <stdio.h>

typedef struct
{
  const gchar *output;
  gchar **filenames;
  GSList *split_times;
} TransmuxerOptions;

static gboolean
split_arg_cb (const gchar * option_name, const gchar * value, gpointer data,
    GError ** error)
{
  TransmuxerOptions *options = data;
  GstClockTime *time = NULL;
  guint hours = 0;
  guint mins = 0;
  guint secs = 0;
  guint nsecs = 0;
  gint ret;

  ret = sscanf (value, "%" GST_TIME_FORMAT, &hours, &mins, &secs, &nsecs);

  if (ret < 3 || mins > 59 || secs > 59 || nsecs > 999999999) {
    g_set_error (error, HWANGSAE_TRANSMUXER_ERROR,
        HWANGSAE_TRANSMUXER_ERROR_INVALID_PARAMETER,
        "Invalid split timestamp '%s'", value);
    return FALSE;
  }

  time = g_new (guint64, 1);

  *time = (hours * GST_SECOND * 60 * 60) +
      (mins * GST_SECOND * 60) + (secs * GST_SECOND) + nsecs;

  options->split_times = g_slist_append (options->split_times, time);
  return TRUE;
}

int
main (int argc, char *argv[])
{
  TransmuxerOptions options = { 0 };
  g_autoptr (GOptionGroup) group = NULL;
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GSList) filenames = NULL;
  g_autoptr (HwangsaeTransmuxer) transmuxer = NULL;
  GOptionEntry entries[] = {
    {"output", 'o', 0, G_OPTION_ARG_FILENAME, &options.output, NULL, NULL},
    {"split", 's', 0, G_OPTION_ARG_CALLBACK, split_arg_cb, NULL, NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &options.filenames,
        NULL, NULL},
    {NULL}
  };

  group = g_option_group_new ("Transmuxer options",
      "Options understood by Hwangsae transmuxer", NULL, &options, NULL);
  g_option_group_add_entries (group, entries);

  context = g_option_context_new (NULL);
  g_option_context_set_main_group (context, group);

  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_printerr ("%s\n", error->message);
    return -1;
  }

  if (!options.filenames) {
    g_printerr ("You must specify files to merge\n");
    return 1;
  }

  if (!options.output) {
    g_printerr ("You must specify output file\n");
    return 1;
  }

  {
    gchar **it;

    for (it = options.filenames; *it; ++it) {
      filenames = g_slist_append (filenames, *it);
    }
  }

  transmuxer = hwangsae_transmuxer_new ();

  {
    GSList *it;

    for (it = options.split_times; it; it = it->next) {
      hwangsae_transmuxer_split_at_running_time (transmuxer,
          *(GstClockTime *) it->data);
    }
  }

  hwangsae_transmuxer_merge (transmuxer, filenames, options.output, &error);

  g_slist_free_full (options.split_times, g_free);

  if (error) {
    g_printerr ("File conversion failed: %s\n", error->message);
    return 1;
  }

  return 0;
}
