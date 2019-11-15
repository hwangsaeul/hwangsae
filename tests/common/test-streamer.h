/** 
 *  tests/test-streamer
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

#ifndef __HWANGSAE_TEST_STREAMER_H__
#define __HWANGSAE_TEST_STREAMER_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define HWANGSAE_TYPE_TEST_STREAMER     (hwangsae_test_streamer_get_type ())
G_DECLARE_FINAL_TYPE                    (HwangsaeTestStreamer, hwangsae_test_streamer, HWANGSAE, TEST_STREAMER, GObject)

HwangsaeTestStreamer * hwangsae_test_streamer_new   (void);
void                   hwangsae_test_streamer_start (HwangsaeTestStreamer * self);
void                   hwangsae_test_streamer_pause (HwangsaeTestStreamer * self);
void                   hwangsae_test_streamer_stop  (HwangsaeTestStreamer * self);

G_END_DECLS

#endif /* __HWANGSAE_TEST_STREAMER_H__ */
