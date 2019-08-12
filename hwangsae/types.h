/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 */

#ifndef __HWANGSAE_TYPES_H__
#define __HWANGSAE_TYPES_H__

#ifndef _HWANGSAE_EXTERN
#define _HWANGSAE_EXTERN        extern
#endif

#define HWANGSAE_API_EXPORT     _HWANGSAE_EXTERN

typedef enum {
  HWANGSAE_RETURN_FAIL = -1,
  HWANGSAE_RETURN_OK,
} HwangsaeReturn;

#endif // __HWANGSAE_TYPES_H__
