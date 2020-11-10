/**
 *  Copyright 2019-2020 SK Telecom Co., Ltd.
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

#ifndef __HWANGSAE_TYPES_H__
#define __HWANGSAE_TYPES_H__

#include <gmodule.h>

#ifndef _HWANGSAE_EXTERN
#define _HWANGSAE_EXTERN        extern
#endif

/**
 * SECTION: types
 * @Title: Hwangsae Types
 * @Short_description: Several types used to export APIs
 */

#define HWANGSAE_API_EXPORT     _HWANGSAE_EXTERN

typedef enum {
  HWANGSAE_RETURN_FAIL = -1,
  HWANGSAE_RETURN_OK,
} HwangsaeReturn;

typedef enum {
  HWANGSAE_CONTAINER_MP4,
  HWANGSAE_CONTAINER_TS,
} HwangsaeContainer;

#define HWANGSAE_RELAY_ERROR           (hwangsae_relay_error_quark ())
GQuark hwangsae_relay_error_quark      (void);

typedef enum {
  HWANGSAE_RELAY_ERROR_READ = 1,
  HWANGSAE_RELAY_ERROR_WRITE,
  HWANGSAE_RELAY_ERROR_UNKNOWN_SOCKOPT,
  HWANGSAE_RELAY_ERROR_SOCKOPT,
} HwangsaeRelayError;

#define HWANGSAE_TRANSMUXER_ERROR      (hwangsae_transmuxer_error_quark())
GQuark hwangsae_transmuxer_error_quark (void);

typedef enum {
  HWANGSAE_TRANSMUXER_ERROR_OVERLAP = 1,
  HWANGSAE_TRANSMUXER_ERROR_INVALID_PARAMETER,
  HWANGSAE_TRANSMUXER_ERROR_MISSING_FILE,
} HwangsaeTransmuxerError;

typedef enum {
  HWANGSAE_CALLER_DIRECTION_SINK,
  HWANGSAE_CALLER_DIRECTION_SRC,
} HwangsaeCallerDirection;

typedef enum {
  HWANGSAE_REJECT_REASON_NO_USERNAME,
  HWANGSAE_REJECT_REASON_NO_RESOURCE,
  HWANGSAE_REJECT_REASON_USERNAME_ALREADY_REGISTERED,
  HWANGSAE_REJECT_REASON_AUTHENTICATION,
  HWANGSAE_REJECT_REASON_TOO_MANY_SINKS,
  HWANGSAE_REJECT_REASON_NO_SUCH_SINK,
  HWANGSAE_REJECT_REASON_ENCRYPTION,
  HWANGSAE_REJECT_REASON_CANT_CONNECT_MASTER,
} HwangsaeRejectReason;

#endif // __HWANGSAE_TYPES_H__
