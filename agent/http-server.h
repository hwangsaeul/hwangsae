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

#ifndef __HWANGSAE_HTTP_SERVER_H__
#define __HWANGSAE_HTTP_SERVER_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define HWANGSAE_TYPE_HTTP_SERVER   (hwangsae_http_server_get_type())
G_DECLARE_FINAL_TYPE                (HwangsaeHttpServer, hwangsae_http_server, HWANGSAE, HTTP_SERVER, GObject)

HwangsaeHttpServer     *hwangsae_http_server_new (guint16 port);

gchar                  *hwangsae_http_server_get_url           (HwangsaeHttpServer *server,
                                                                gchar              *edge_id,
                                                                gchar              *file_id);

gchar                  *hwangsae_http_server_check_file_path   (HwangsaeHttpServer *server,
                                                                gchar              *edge_id,
                                                                gchar              *file_id);

G_END_DECLS

#endif /* __HWANGSAE_HTTP_SERVER_H__ */
