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

#ifndef __HWANGSAE_TRANSMUXER_H__
#define __HWANGSAE_TRANSMUXER_H__

#if !defined(__HWANGSAE_INSIDE__) && !defined(HWANGSAE_COMPILATION)
#error "Only <hwangsae/hwangsae.h> can be included directly."
#endif

#include <glib-object.h>

G_BEGIN_DECLS

#define HWANGSAE_TYPE_TRANSMUXER  (hwangsae_transmuxer_get_type ())
G_DECLARE_FINAL_TYPE              (HwangsaeTransmuxer, hwangsae_transmuxer, HWANGSAE, TRANSMUXER, GObject)

HwangsaeTransmuxer     *hwangsae_transmuxer_new          (void);

G_END_DECLS

#endif // __HWANGSAE_TRANSMUXER_H__
