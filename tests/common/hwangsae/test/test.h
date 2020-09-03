/** 
 *  tests/test
 *
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

#ifndef __HWANGSAE_TEST_H__
#define __HWANGSAE_TEST_H__

#include <gst/gst.h>
#include <hwangsae/hwangsae.h>
#include <hwangsae/test/test-streamer.h>

G_BEGIN_DECLS

GstClockTime          hwangsae_test_get_file_duration (const gchar * file_path);
GstClockTimeDiff      hwangsae_test_get_gap_duration  (const gchar * file_path);

gchar                *hwangsae_test_build_source_uri  (HwangsaeTestStreamer * streamer,
                                                       HwangsaeRelay        * relay,
                                                       const gchar          * username);

GstElement           *hwangsae_test_make_receiver     (HwangsaeTestStreamer * streamer,
                                                       HwangsaeRelay        * relay,
                                                       const gchar          * username);

G_END_DECLS

#endif /* __HWANGSAE_TEST_H__ */
