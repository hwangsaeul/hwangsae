/**
 *  Copyright 2019 SK Telecom Co., Ltd.
 *    Author: Jakub Adam <jakub.adam@collabora.com> 
 *    Author: Walter Lozano <walter.lozano@collabora.com>
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

#include "http-server.h"

#include <glib/gstdio.h>

#include <json-glib/json-glib.h>

#include <libsoup/soup-message.h>
#include <libsoup/soup-server.h>
#include <errno.h>

struct _HwangsaeHttpServer
{
  GObject parent;

  SoupServer *soup_server;

  gchar *recording_dir;
};

G_DEFINE_TYPE (HwangsaeHttpServer, hwangsae_http_server, G_TYPE_OBJECT)
     HwangsaeHttpServer *hwangsae_http_server_new ()
{
  return HWANGSAE_HTTP_SERVER (g_object_new (HWANGSAE_TYPE_HTTP_SERVER, NULL));
}

gchar *
hwangsae_http_server_get_recording_dir (HwangsaeHttpServer * server)
{
  return server->recording_dir;
}

void
hwangsae_http_server_set_recording_dir (HwangsaeHttpServer * server,
    gchar * recording_dir)
{
  g_free (server->recording_dir);
  server->recording_dir = strdup (recording_dir);
}

static gchar *
get_file_path (gchar * base_path, gchar * record_id, gchar * ext)
{
  gchar *file_path;

  file_path =
      g_strdup_printf ("%s/hwangsae-recording-%s-00000.%s", base_path,
      record_id, ext);

  return file_path;
}

gchar *
check_file_path (gchar * base_path, gchar * record_id)
{
  gchar *file_path;

  file_path = get_file_path (base_path, record_id, "ts");
  if (g_file_test (file_path, G_FILE_TEST_EXISTS))
    return file_path;

  g_free (file_path);
  file_path = get_file_path (base_path, record_id, "mp4");
  if (g_file_test (file_path, G_FILE_TEST_EXISTS))
    return file_path;

  g_free (file_path);

  return NULL;
}

static void
http_cb (SoupServer * server, SoupMessage * msg, const char *path,
    GHashTable * query, SoupClientContext * client, gpointer user_data)
{
  HwangsaeHttpServer *self = HWANGSAE_HTTP_SERVER (user_data);
  g_autofree gchar *record_id = NULL;
  g_autofree gchar *file_path = NULL;
  GMappedFile *mapping;
  GStatBuf st;
  SoupBuffer *buffer;
  GError *err = NULL;

  if (msg->method != SOUP_METHOD_GET) {
    soup_message_set_status (msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
    return;
  }

  g_debug ("request path: %s", path);

  record_id = g_strdup (path + 1);

  file_path = check_file_path (self->recording_dir, record_id);
  if (!file_path) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
    return;
  }

  g_debug ("record_id: %s file: %s", record_id, file_path);

  if (g_stat (file_path, &st) == -1) {
    g_debug ("file %s cannot be accessed", file_path);
    if (errno == EPERM)
      soup_message_set_status (msg, SOUP_STATUS_FORBIDDEN);
    else if (errno == ENOENT)
      soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
    else
      soup_message_set_status (msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
    return;
  }

  mapping = g_mapped_file_new (file_path, FALSE, &err);
  if (!mapping) {
    g_warning ("%s", err->message);
    soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
    return;
  }

  buffer = soup_buffer_new_with_owner (g_mapped_file_get_contents (mapping),
      g_mapped_file_get_length (mapping),
      mapping, (GDestroyNotify) g_mapped_file_unref);
  soup_message_body_append_buffer (msg->response_body, buffer);
  soup_buffer_free (buffer);
  soup_message_set_status (msg, SOUP_STATUS_OK);
}

static void
hwangsae_http_server_init (HwangsaeHttpServer * server)
{
  GError *error = NULL;

  server->soup_server = soup_server_new (NULL, NULL);
  server->recording_dir = NULL;
  g_assert_no_error (error);

  soup_server_add_handler (server->soup_server, NULL, http_cb, server, NULL);

  soup_server_listen_all (server->soup_server, 8090, 0, &error);
  g_assert_no_error (error);
}

static void
hwangsae_http_server_dispose (GObject * object)
{
  HwangsaeHttpServer *self = HWANGSAE_HTTP_SERVER (object);

  soup_server_disconnect (self->soup_server);
  g_clear_object (&self->soup_server);
  g_clear_pointer (&self->recording_dir, g_free);
}

static void
hwangsae_http_server_class_init (HwangsaeHttpServerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = hwangsae_http_server_dispose;
}
