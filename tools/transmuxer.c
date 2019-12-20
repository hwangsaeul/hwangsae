/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
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

typedef struct
{
  const gchar *output;
  gchar **filenames;
} TransmuxerOptions;

int
main (int argc, char *argv[])
{
  TransmuxerOptions options = { 0 };
  g_autoptr (GOptionGroup) group = NULL;
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GSList) filenames = NULL;
  g_autoptr (HwangsaeTransmuxer) transmuxer = NULL;
  gchar **it;
  GOptionEntry entries[] = {
    {"output", 'o', 0, G_OPTION_ARG_FILENAME, &options.output, NULL, NULL},
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

  for (it = options.filenames; *it; ++it) {
    filenames = g_slist_append (filenames, *it);
  }

  transmuxer = hwangsae_transmuxer_new ();
  hwangsae_transmuxer_merge (transmuxer, filenames, options.output, &error);

  if (error) {
    g_printerr ("File conversion failed: %s\n", error->message);
    return 1;
  }

  return 0;
}
