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

#include <gst/gstclock.h>

G_BEGIN_DECLS

#define HWANGSAE_TYPE_TRANSMUXER  (hwangsae_transmuxer_get_type ())
G_DECLARE_FINAL_TYPE              (HwangsaeTransmuxer, hwangsae_transmuxer, HWANGSAE, TRANSMUXER, GObject)

HwangsaeTransmuxer     *hwangsae_transmuxer_new          (void);

/**
 * hwangsae_transmuxer_merge:
 * @transmuxer: a pointer to a HwangsaeRecorder object
 * @input_files: list of files to use as input
 * @output: output file
 * @error: a #GError
 *
 * Starts the merging process.
 *
 * When time- or size-based splitting is enabled, @output can contain a pattern
 * that gets replaced with current segment number. For example 'out-%d.mp4' will
 * create out-0.mp4, out-1.mp4, etc.. Otherwise the segment number will be put
 * as a suffix to each output filename (out.mp4.00000, out.mp4.00001, etc.).
 */
void                    hwangsae_transmuxer_merge        (HwangsaeTransmuxer  *transmuxer,
                                                          GSList              *input_files,
                                                          const gchar         *output,
                                                          GError             **error);

/**
 * hwangsae_recorder_set_max_size_time:
 * @self: a pointer to a HwangsaeRecorder object
 * @duration_ns: duration in nanoseconds
 *
 * Sets the maximun duration of recorded video in a single file to duration_ns nanoseconds.
 */
void                    hwangsae_transmuxer_set_max_size_time
                                                       (HwangsaeTransmuxer    *self,
                                                        guint64                duration_ns);

/**
 * hwangsae_recorder_get_max_size_time:
 * @self: a pointer to a HwangsaeRecorder object
 *
 * Gets the maximun duration of recorded video in a single file
 *
 * Returns: the maximun duration
 */
guint64                 hwangsae_transmuxer_get_max_size_time
                                                       (HwangsaeTransmuxer    *self);

/**
 * hwangsae_recorder_set_max_size_bytes:
 * @self: a pointer to a HwangsaeRecorder object
 * @bytes: size in bytes
 *
 * Sets the maximun size of recorded video in a single file
 *
 * Returns: the maximun size
 */
void                    hwangsae_transmuxer_set_max_size_bytes
                                                       (HwangsaeTransmuxer    *self,
                                                        guint64                bytes);

/**
 * hwangsae_recorder_get_max_size_bytes:
 * @self: a pointer to a HwangsaeRecorder object
 *
 * Gets the maximun size of recorded video in a single file
 *
 * Returns: the maximun size
 */
 guint64                 hwangsae_transmuxer_get_max_size_bytes
                                                       (HwangsaeTransmuxer    *self);

/**
 * hwangsae_transmuxer_split_at_running_time:
 * @self: a HwangsaeRecorder object
 * @time: split timestamp
 *
 * Orders the transmuxer to split the output file when the recording reaches
 * @time. If the function is called multiple times, a split will occur on each
 * passed timestamp.
 */
void                     hwangsae_transmuxer_split_at_running_time
                                                       (HwangsaeTransmuxer    *self,
                                                        GstClockTime           time);

G_END_DECLS

#endif // __HWANGSAE_TRANSMUXER_H__
