/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jakub Adam <jakub.adam@collabora.com>
 *
 */

#include "hwangsae/recorder.h"

#include <glib-unix.h>
#include <gio/gio.h>
#include <gst/gst.h>

typedef struct
{
  const gchar *uri;
} RecorderOptions;

static void
activate (GApplication * app, gpointer user_data)
{
  HwangsaeRecorder *recorder = user_data;
  RecorderOptions *options;
  g_autoptr (GError) error = NULL;

  g_return_if_fail (recorder);

  g_application_hold (app);

  options = g_object_get_data (G_OBJECT (app), "options");
  hwangsae_recorder_start_recording (recorder, options->uri);
}

static gboolean
intr_handler (gpointer user_data)
{
  HwangsaeRecorder *recorder = user_data;
  g_autoptr (GError) error = NULL;

  hwangsae_recorder_stop_recording (recorder);

  return G_SOURCE_REMOVE;
}

static void
stream_connected_cb (HwangsaeRecorder * recorder, gpointer unused)
{
  g_print ("Stream connected\n");
}


static void
file_created_cb (HwangsaeRecorder * recorder, const gchar * file_path,
    gpointer unused)
{
  g_print ("Recording to file %s\n", file_path);
}

static void
file_completed_cb (HwangsaeRecorder * recorder, const gchar * file_path,
    GApplication * app)
{
  g_application_release (app);
}

static gboolean
uri_arg_cb (const gchar * option_name, const gchar * value, gpointer data,
    GError ** error)
{
  RecorderOptions *options = data;

  if (!options->uri) {
    options->uri = g_strdup (value);
  }

  return TRUE;
}

int
main (int argc, char *argv[])
{
  RecorderOptions options;

  g_autoptr (GError) error = NULL;
  g_autoptr (GApplication) app =
      g_application_new ("org.hwangsaeul.Hwangsae1.RecorderApp", 0);

  g_autoptr (GOptionGroup) group = NULL;
  g_autoptr (GOptionContext) context = NULL;
  GOptionEntry entries[] = {
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_CALLBACK, uri_arg_cb, NULL, NULL},
    {NULL}
  };

  g_autoptr (HwangsaeRecorder) recorder = NULL;

  options.uri = NULL;

  gst_init (&argc, &argv);

  group = g_option_group_new ("FIFO transmit options",
      "Options understood by Gaeguli FIFO transmit", NULL, &options, NULL);
  g_option_group_add_entries (group, entries);

  context = g_option_context_new (NULL);
  g_option_context_set_main_group (context, group);

  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_printerr ("%s\n", error->message);
    return -1;
  }

  if (!options.uri) {
    g_printerr ("You must specify stream URI to record\n");
    return 1;
  }

  recorder = hwangsae_recorder_new ();
  g_signal_connect (recorder, "stream-connected",
      (GCallback) stream_connected_cb, NULL);
  g_signal_connect (recorder, "file-created",
      (GCallback) file_created_cb, NULL);
  g_signal_connect (recorder, "file-completed",
      (GCallback) file_completed_cb, app);

  g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, recorder);

  g_signal_connect (app, "activate", G_CALLBACK (activate), recorder);

  g_object_set_data (G_OBJECT (app), "options", &options);

  return g_application_run (app, argc, argv);
}
