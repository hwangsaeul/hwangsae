/**
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

#include "config.h"

#include "agent.h"
#include <hwangsae/relay.h>

#include <glib-unix.h>

struct _HwangsaeAgent
{
  GApplication parent;

  HwangsaeRelay *relay;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (HwangsaeAgent, hwangsae_agent, G_TYPE_APPLICATION)
/* *INDENT-ON* */

static void
hwangsae_agent_dispose (GObject * object)
{
  HwangsaeAgent *self = HWANGSAE_AGENT (object);

  g_clear_object (&self->relay);

  G_OBJECT_CLASS (hwangsae_agent_parent_class)->dispose (object);
}

static void
hwangsae_agent_class_init (HwangsaeAgentClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = hwangsae_agent_dispose;
}

static void
hwangsae_agent_init (HwangsaeAgent * self)
{
  self->relay = hwangsae_relay_new ();
}

static gboolean
signal_handler (GApplication * app)
{
  g_application_release (app);

  return G_SOURCE_REMOVE;
}

int
main (int argc, char *argv[])
{
  g_autoptr (GApplication) app = NULL;

  app = G_APPLICATION (g_object_new (HWANGSAE_TYPE_AGENT,
          "application-id", "org.hwangsaeul.Hwangsae1",
          "flags", G_APPLICATION_IS_SERVICE, NULL));

  g_unix_signal_add (SIGINT, (GSourceFunc) signal_handler, app);

  g_application_hold (app);

  return g_application_run (app, argc, argv);
}
