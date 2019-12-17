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

#include "transmuxer.h"

struct _HwangsaeTransmuxer
{
  GObject parent;
};

typedef struct
{
  int i;
} HwangsaeTransmuxerPrivate;

/* *INDENT-OFF* */
G_DEFINE_TYPE_WITH_PRIVATE (HwangsaeTransmuxer, hwangsae_transmuxer, G_TYPE_OBJECT)
/* *INDENT-ON* */

HwangsaeTransmuxer *
hwangsae_transmuxer_new (void)
{
  return g_object_new (HWANGSAE_TYPE_TRANSMUXER, NULL);
}

static void
hwangsae_transmuxer_init (HwangsaeTransmuxer * self)
{
}

static void
hwangsae_transmuxer_class_init (HwangsaeTransmuxerClass * klass)
{
}
