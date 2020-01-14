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

#include <hwangsae/common.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-server.h>
#include <errno.h>

struct _HwangsaeHttpServer
{
  GObject parent;

  SoupServer *soup_server;

  guint port;

  gchar *recording_dir;
  gchar *external_ip;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (HwangsaeHttpServer, hwangsae_http_server, G_TYPE_OBJECT)
/* *INDENT-ON* */

enum
{
  PROP_PORT = 1,
  PROP_RECORDING_DIR,
  PROP_EXTERNAL_IP,
  PROP_LAST
};

HwangsaeHttpServer *
hwangsae_http_server_new (guint16 port)
{
  return HWANGSAE_HTTP_SERVER (g_object_new (HWANGSAE_TYPE_HTTP_SERVER,
          "port", port, NULL));
}

static gchar *
get_file_path (gchar * base_path, gchar * file_id, gchar * ext)
{
  g_autofree gchar *filename = NULL;
  gchar *file_path;

  filename = g_strdup_printf ("hwangsae-recording-%s.%s", file_id, ext);

  file_path = g_build_filename (base_path, filename, NULL);

  return file_path;
}

gchar *
hwangsae_http_server_check_file_path (HwangsaeHttpServer * server,
    gchar * edge_id, gchar * file_id)
{
  g_autofree gchar *recording_edge_dir;
  gchar *file_path;

  recording_edge_dir = g_build_filename (server->recording_dir, edge_id, NULL);

  file_path = get_file_path (recording_edge_dir, file_id, "ts");
  if (g_file_test (file_path, G_FILE_TEST_EXISTS))
    return file_path;

  g_free (file_path);
  file_path = get_file_path (recording_edge_dir, file_id, "mp4");
  if (g_file_test (file_path, G_FILE_TEST_EXISTS))
    return file_path;

  g_free (file_path);

  return NULL;
}

gchar *
hwangsae_http_server_get_url (HwangsaeHttpServer * server, gchar * edge_id,
    gchar * file_id)
{
  g_autofree gchar *file_path = NULL;
  g_autofree gchar *local_ip = NULL;

  file_path = hwangsae_http_server_check_file_path (server, edge_id, file_id);

  if (!file_path) {
    return g_strdup ("");
  }

  if (server->external_ip && g_strcmp0 (server->external_ip, ""))
    local_ip = g_strdup (server->external_ip);
  else
    local_ip = hwangsae_common_get_local_ip ();

  return g_strdup_printf ("http://%s:%d/%s/%s", local_ip, server->port, edge_id,
      file_id);
}

static void
http_cb (SoupServer * server, SoupMessage * msg, const char *path,
    GHashTable * query, SoupClientContext * client, gpointer user_data)
{
  HwangsaeHttpServer *self = HWANGSAE_HTTP_SERVER (user_data);
  g_autofree gchar *edge_id = NULL;
  g_autofree gchar *file_id = NULL;
  g_autofree gchar *recording_edge_dir = NULL;
  g_autofree gchar *file_path = NULL;
  g_autofree gchar *file_name = NULL;
  g_autoptr (GHashTable) params = NULL;
  GMappedFile *mapping;
  GStatBuf st;
  SoupBuffer *buffer;
  GError *err = NULL;
  gchar **parts;
  guint parts_len;

  if (msg->method != SOUP_METHOD_GET) {
    soup_message_set_status (msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
    return;
  }

  g_debug ("request path: %s", path);

  parts = g_strsplit (path, "/", -1);

  parts_len = g_strv_length (parts);

  if (parts_len != 3) {
    g_strfreev (parts);
    soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
    return;
  }

  edge_id = g_strdup (parts[1]);
  file_id = g_strdup (parts[2]);

  g_strfreev (parts);

  file_path = hwangsae_http_server_check_file_path (self, edge_id, file_id);
  if (!file_path) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
    return;
  }

  g_debug ("file path: %s", file_path);

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

  params = g_hash_table_new (g_str_hash, g_str_equal);
  file_name = g_path_get_basename (file_path);
  g_hash_table_insert (params, "filename", file_name);
  soup_message_headers_set_content_disposition (msg->response_headers,
      "attachment", params);
  soup_message_headers_append (msg->response_headers,
      "Access-Control-Allow-Origin", "*");
}

static void
hwangsae_http_server_init (HwangsaeHttpServer * self)
{
}

static void
hwangsae_http_server_constructed (GObject * object)
{
  HwangsaeHttpServer *self = HWANGSAE_HTTP_SERVER (object);
  GError *error = NULL;

  self->recording_dir = NULL;
  self->external_ip = NULL;

  self->soup_server = soup_server_new (NULL, NULL);
  soup_server_add_handler (self->soup_server, NULL, http_cb, self, NULL);

  soup_server_listen_all (self->soup_server, self->port, 0, &error);
  g_assert_no_error (error);

  g_debug ("HTTP server listening on port %u", self->port);
}

static void
hwangsae_http_server_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  HwangsaeHttpServer *self = HWANGSAE_HTTP_SERVER (object);

  switch (property_id) {
    case PROP_PORT:
      self->port = g_value_get_uint (value);
      break;
    case PROP_RECORDING_DIR:
      g_clear_pointer (&self->recording_dir, g_free);
      self->recording_dir = g_strdup (g_value_get_string (value));
      break;
    case PROP_EXTERNAL_IP:
    {
      const gchar *ip = g_value_get_string (value);
      g_clear_pointer (&self->external_ip, g_free);
      if (ip && ip[0] != '\0') {
        self->external_ip = g_strdup (ip);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
hwangsae_http_server_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  HwangsaeHttpServer *self = HWANGSAE_HTTP_SERVER (object);

  switch (property_id) {
    case PROP_PORT:
      g_value_set_uint (value, self->port);
      break;
    case PROP_RECORDING_DIR:
      g_value_set_string (value, self->recording_dir);
      break;
    case PROP_EXTERNAL_IP:
      g_value_set_string (value, self->external_ip);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
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

  gobject_class->constructed = hwangsae_http_server_constructed;
  gobject_class->set_property = hwangsae_http_server_set_property;
  gobject_class->get_property = hwangsae_http_server_get_property;
  gobject_class->dispose = hwangsae_http_server_dispose;

  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_uint ("port", "Server listen port", "Server listen port",
          0, G_MAXUINT16, 8080,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RECORDING_DIR,
      g_param_spec_string ("recording-dir", "Recording Directory",
          "Recording Directory", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_EXTERNAL_IP,
      g_param_spec_string ("external-ip", "External IP address",
          "External IP address", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}
