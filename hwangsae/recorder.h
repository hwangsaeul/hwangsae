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

/**
 * SECTION: recorder
 * @Title: HwangsaeRecorder
 * @Short_description: Object to record SRT streaming
 *
 * A #HwangsaeRecorder is object capable of receve SRT streaming and record it in local filesystem.
 */

 G_BEGIN_DECLS

#define HWANGSAE_TYPE_RECORDER  (hwangsae_recorder_get_type ())
G_DECLARE_FINAL_TYPE            (HwangsaeRecorder, hwangsae_recorder, HWANGSAE, RECORDER, GObject)

/**
 * hwangsae_recorder_new:
 *
 * Creates a new HwangsaeRecorder object
 *
 * Returns: the newly created object
 */
 HwangsaeRecorder       *hwangsae_recorder_new          (void);

/**
 * hwangsae_recorder_set_container:
 * @self: a pointer to a HwangsaeRecorder object
 * @container: container to use
 *
 * Sets the container format to use
 */
void                    hwangsae_recorder_set_container(HwangsaeRecorder * self,
                                                        HwangsaeContainer container);

/**
 * hwangsae_recorder_get_container:
 * @self: a pointer to a HwangsaeRecorder object
 *
 * Gets the container format to use
 *
 * Returns: the container format to use
 */
HwangsaeContainer       hwangsae_recorder_get_container(HwangsaeRecorder * self);

/**
 * hwangsae_recorder_set_max_size_time:
 * @self: a pointer to a HwangsaeRecorder object
 * @duration_ns: duration in nanoseconds
 *
 * Sets the maximun duration of recorded video in a single file to duration_ns nanoseconds.
 */
void                    hwangsae_recorder_set_max_size_time
                                                       (HwangsaeRecorder * self,
                                                        guint64            duration_ns);

/**
 * hwangsae_recorder_get_max_size_time:
 * @self: a pointer to a HwangsaeRecorder object
 *
 * Gets the maximun duration of recorded video in a single file
 *
 * Returns: the maximun duration
 */
guint64                 hwangsae_recorder_get_max_size_time
                                                       (HwangsaeRecorder * self);

/**
 * hwangsae_recorder_set_max_size_bytes:
 * @self: a pointer to a HwangsaeRecorder object
 * @bytes: size in bytes
 *
 * Sets the maximun size of recorded video in a single file
 *
 * Returns: the maximun size
 */
void                    hwangsae_recorder_set_max_size_bytes
                                                       (HwangsaeRecorder * self,
                                                        guint64            bytes);

/**
 * hwangsae_recorder_get_max_size_bytes:
 * @self: a pointer to a HwangsaeRecorder object
 *
 * Gets the maximun size of recorded video in a single file
 *
 * Returns: the maximun size
 */
 guint64                 hwangsae_recorder_get_max_size_bytes
                                                       (HwangsaeRecorder * self);

/**
 * hwangsae_recorder_set_recording_dir:
 * @self: a pointer to a HwangsaeRecorder object
 * @recording_dir: path to the folder to store recordings
 *
 * Sets the folder used to save recordings
 */
 void                    hwangsae_recorder_set_recording_dir
                                                       (HwangsaeRecorder * self,
                                                        gchar *recording_dir);

/**
 * hwangsae_recorder_get_recording_dir:
 * @self: a pointer to a HwangsaeRecorder object
 *
 * Gets the folder used to save recordings
 *
 * Returns: the folder path
 */
 gchar                  *hwangsae_recorder_get_recording_dir
                                                       (HwangsaeRecorder * self);

/**
 * hwangsae_recorder_set_filename_prefix:
 * @self: a pointer to a HwangsaeRecorder object
 * @filename_prefix: prefix used for filename
 *
 * Sets the prefix to use for naming recording files
 */
 void                    hwangsae_recorder_set_filename_prefix
                                                       (HwangsaeRecorder * self,
                                                        gchar *filename_prefix);

/**
 * hwangsae_recorder_get_filename_prefix:
 * @self: a pointer to a HwangsaeRecorder object
 *
 * Gets the prefix to use for naming recording files
 *
 * Returns: the prefix for naming
 */
 gchar                  *hwangsae_recorder_get_filename_prefix
                                                       (HwangsaeRecorder * self);

/**
 * hwangsae_recorder_start_recording:
 * @self: a pointer to a HwangsaeRecorder object
 * @uri: source streaming URI
 *
 * Starts a new recording using using the provided source URI
 */
 void                    hwangsae_recorder_start_recording
                                                       (HwangsaeRecorder * self,
                                                        const gchar * uri);

/**
 * hwangsae_recorder_stop_recording:
 * @self: a pointer to a HwangsaeRecorder object
 *
 * Stops the current recording
 */
 void                    hwangsae_recorder_stop_recording
                                                       (HwangsaeRecorder * self);

G_END_DECLS

#endif // __HWANGSAE_RECORDER_H__
