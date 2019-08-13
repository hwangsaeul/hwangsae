/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 */

#include "config.h"

#include "agent.h"

struct _HwangsaeAgent
{
  GApplication parent;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (HwangsaeAgent, hwangsae_agent, G_TYPE_APPLICATION)
/* *INDENT-ON* */

static void
hwangsae_agent_class_init (HwangsaeAgentClass * klass)
{
}

static void
hwangsae_agent_init (HwangsaeAgent * self)
{
}

int
main (int argc, char *argv[])
{
  g_autoptr (GApplication) app = NULL;

  app = G_APPLICATION (g_object_new (HWANGSAE_TYPE_AGENT,
          "application-id", "org.hwangsaeul.Hwangsae1",
          "flags", G_APPLICATION_IS_SERVICE, NULL));

  return g_application_run (app, argc, argv);
}
