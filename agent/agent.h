/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 */

#ifndef __HWANGSAE_AGENT_H__
#define __HWANGSAE_AGENT_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define HWANGSAE_TYPE_AGENT             (hwangsae_agent_get_type ())
G_DECLARE_FINAL_TYPE                    (HwangsaeAgent, hwangsae_agent, HWANGSAE, AGENT, GApplication)

G_END_DECLS

#endif // __HWANGSAE_AGENT_H__
