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

#include <hwangsae/types.h>

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

void                    hwangsae_relay_set_latency      (HwangsaeRelay *relay,
                                                         HwangsaeCallerDirection
                                                                        direction,
                                                         gint           latency);
                                                                        
/**
 * hwangsae_relay_start:
 * @relay: a HwangsaeRelay object
 *
 * Makes the relay start listening on sink and source ports and forwarding
 * the streams.
 */
void                    hwangsae_relay_start            (HwangsaeRelay *relay);

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

/**
 * hwangsae_relay_get_socket_option:
 * @relay: a HwangsaeRelay object
 * @socket_id: a caller's SRT socket ID obtained from "caller-accepted" signal
 * @option: ID of a SRT socket option. See libsrt API for supported options.
 * @error: a location to receive a GError if the call fails
 *
 * Returns: The socket option value as a GVariant. On error returns NULL and
 * sets @error.
 */
GVariant               *hwangsae_relay_get_socket_option(HwangsaeRelay *relay,
                                                         gint           socket_id,
                                                         gint           option,
                                                         GError       **error);

/**
 * hwangsae_relay_set_socket_option:
 * @relay: a HwangsaeRelay object
 * @socket_id: a caller's SRT socket ID obtained from "caller-accepted" signal
 * @option: ID of a SRT socket option. See libsrt API for supported options.
 * @value: new value of the option
 * @error: a location to receive a GError if the call fails
 *
 * Returns: TRUE when the option has been updated successfully.
 */
gboolean                hwangsae_relay_set_socket_option(HwangsaeRelay *relay,
                                                         gint           socket_id,
                                                         gint           option,
                                                         GVariant      *value,
                                                         GError       **error);

/**
 * hwangsae_relay_disconnect_sink:
 * @relay: a HwangsaeRelay object
 * @username: SRT Stream ID username of the sink to disconnect
 *
 * Disconnects any sink that matches @username from @relay.
 */
void                    hwangsae_relay_disconnect_sink  (HwangsaeRelay *relay,
                                                         const gchar   *username);

/**
 * hwangsae_relay_disconnect_source:
 * @relay: a HwangsaeRelay object
 * @username: SRT Stream ID username of the sources to disconnect
 * @resource: SRT Stream ID username of the sources to disconnect
 *
 * Disconnects sources that match @username from @relay. If @resource is not
 * NULL, only those @username sources that are connected to @resource get
 * disconnected.
 */
void                    hwangsae_relay_disconnect_source(HwangsaeRelay *relay,
                                                         const gchar   *username,
                                                         const gchar   *resource);

G_END_DECLS

#endif // __HWANGSAE_RELAY_H__
