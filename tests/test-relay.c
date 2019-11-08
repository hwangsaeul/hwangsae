/** 
 *  tests/test-relay
 *
 *  Copyright 2019 SK Telecom Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
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

#include "hwangsae/hwangsae.h"

static void
test_hwangsae_relay_instance (void)
{
  guint sink_port, source_port;
  g_autoptr (HwangsaeRelay) relay = hwangsae_relay_new ();

  g_assert_nonnull (relay);

  g_object_get (relay, "sink-port", &sink_port, "source-port", &source_port,
      NULL);

  g_assert_cmpint (sink_port, ==, 8888);
  g_assert_cmpint (source_port, ==, 9999);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  /* Don't treat warnings as fatal, which is GTest default. */
  g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);

  g_test_add_func ("/hwangsae/relay-instance", test_hwangsae_relay_instance);

  return g_test_run ();
}
