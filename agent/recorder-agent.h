/**
 *  Copyright 2019 SK Telecom Co., Ltd.
 *    Author: Walter Lozano <walter.lozano@collabora.com>
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


#ifndef __HWANGSAE_RECORDER_AGENT_H__
#define __HWANGSAE_RECORDER_AGENT_H__

#include <hwangsaeul/application.h>

G_BEGIN_DECLS
#ifndef _RECORDER_AGENT_EXTERN
#define _RECORDER_AGENT_EXTERN         extern
#endif
#define RECORDER_AGENT_API_EXPORT      _RECORDER_AGENT_EXTERN
#define HWANGSAE_TYPE_RECORDER_AGENT    (hwangsae_recorder_agent_get_type ())
/* *INDENT-OFF* */
RECORDER_AGENT_API_EXPORT
G_DECLARE_DERIVABLE_TYPE (HwangsaeRecorderAgent, hwangsae_recorder_agent,
    HWANGSAE, RECORDER_AGENT, HwangsaeulApplication)
/* *INDENT-ON* */
    typedef enum
{
  RELAY_METHOD_NONE = 0,
  RELAY_METHOD_START_STREAMING,
  RELAY_METHOD_STOP_STREAMING
} RelayMethods;

struct _HwangsaeRecorderAgentClass
{
  HwangsaeulApplicationClass parent_class;

  gint64      (* start_recording)                 (HwangsaeRecorderAgent * self,
                                                   gchar * edge_id);

  void        (* stop_recording)                  (HwangsaeRecorderAgent * self,
                                                   gchar * edge_id);
};

gchar         *hwangsae_recorder_agent_get_recording_dir
                                                 (HwangsaeRecorderAgent * self);

gchar         *hwangsae_recorder_agent_get_recorder_id
                                                 (HwangsaeRecorderAgent * self);

gchar         *hwangsae_recorder_agent_get_relay_address
                                                 (HwangsaeRecorderAgent * self);

guint          hwangsae_recorder_agent_get_relay_stream_port
                                                 (HwangsaeRecorderAgent * self);

void           hwangsae_recorder_agent_send_rest_api
                                                 (HwangsaeRecorderAgent * self,
                                                  RelayMethods method, 
                                                  gchar * edge_id);

G_END_DECLS
#endif // __HWANGSAE_RECORDER_AGENT_H__
