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

#ifndef __HWANGSAE_RELAY_H__
#define __HWANGSAE_RELAY_H__

#if !defined(__HWANGSAE_INSIDE__) && !defined(HWANGSAE_COMPILATION)
#error "Only <hwangsae/hwangsae.h> can be included directly."
#endif

#include <glib-object.h>

/**
 * SECTION: relay
 * @Title: HwangsaeRelay
 * @Short_description: Object to handle SRT streaming relay
 *
 * A #HwangsaeRelay is object capable of handinling SRT streaming relay, allowing one stream to be shared by N different clients.
 */

G_BEGIN_DECLS

#define HWANGSAE_TYPE_RELAY     (hwangsae_relay_get_type ())
G_DECLARE_FINAL_TYPE            (HwangsaeRelay, hwangsae_relay, HWANGSAE, RELAY, GObject)

/**
 * hwangsae_relay_new:
 * @external_ip: an external ip address
 * @sink_port: an inbound network port
 * @source_port: an outbound network port
 *
 * Creates a new HwangsaeRelay object
 *
 * Returns: the newly created object
 */
HwangsaeRelay          *hwangsae_relay_new              (const gchar   *external_ip,
                                                         guint          sink_port,
                                                         guint          source_port);

/**
 * hwangsae_relay_get_sink_uri:
 * @relay: a pointer to a HwangsaeRelay object
 *
 * Gets the sink URI
 *
 * Returns: the sink URI
 */
const gchar            *hwangsae_relay_get_sink_uri     (HwangsaeRelay *relay);

/**
 * hwangsae_relay_get_source_uri:
 * @relay: a pointer to a HwangsaeRelay object
 *
 * Gets the source URI
 *
 * Returns: the source URI
 */
 const gchar            *hwangsae_relay_get_source_uri   (HwangsaeRelay *relay);

G_END_DECLS

#endif // __HWANGSAE_RELAY_H__
