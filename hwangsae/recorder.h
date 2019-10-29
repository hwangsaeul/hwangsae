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

#ifndef __HWANGSAE_RECORDER_H__
#define __HWANGSAE_RECORDER_H__

#if !defined(__HWANGSAE_INSIDE__) && !defined(HWANGSAE_COMPILATION)
#error "Only <hwangsae/hwangsae.h> can be included directly."
#endif

#include <glib-object.h>

#include "types.h"

G_BEGIN_DECLS

#define HWANGSAE_TYPE_RECORDER  (hwangsae_recorder_get_type ())
G_DECLARE_FINAL_TYPE            (HwangsaeRecorder, hwangsae_recorder, HWANGSAE, RECORDER, GObject)

HwangsaeRecorder       *hwangsae_recorder_new          (void);

void                    hwangsae_recorder_set_container(HwangsaeRecorder * self,
                                                        HwangsaeContainer container);
HwangsaeContainer       hwangsae_recorder_get_container(HwangsaeRecorder * self);

void                    hwangsae_recorder_set_max_size_time
                                                       (HwangsaeRecorder * self,
                                                        guint64            duration_ns);

guint64                 hwangsae_recorder_get_max_size_time
                                                       (HwangsaeRecorder * self);

void                    hwangsae_recorder_start_recording
                                                       (HwangsaeRecorder * self,
                                                        const gchar * uri);

void                    hwangsae_recorder_stop_recording
                                                       (HwangsaeRecorder * self);

G_END_DECLS

#endif // __HWANGSAE_RECORDER_H__
