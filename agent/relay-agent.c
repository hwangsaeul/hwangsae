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

#include "relay-agent.h"
#include <hwangsae/relay.h>

#include <glib-unix.h>

#include <hwangsae/dbus/manager-generated.h>
#include <hwangsae/dbus/edge-interface-generated.h>
#include <chamge/chamge.h>

#define DEFAULT_HUB_UID        "abc-987-123"
#define DEFAULT_BACKEND         CHAMGE_BACKEND_AMQP
#define DEFAULT_URI            "srt://127.0.0.1:8888"

struct _HwangsaeRelayAgent
{
  HwangsaeulApplication parent;

  HwangsaeRelay *relay;
  Hwangsae1DBusManager *manager;
  Hwangsae1DBusEdgeInterface *edge_interface;
  ChamgeHub *chamge_hub;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (HwangsaeRelayAgent, hwangsae_relay_agent, HWANGSAEUL_TYPE_APPLICATION)
/* *INDENT-ON* */

static gboolean
hwangsae_relay_agent_dbus_register (GApplication * app,
    GDBusConnection * connection, const gchar * object_path, GError ** error)
{
  gboolean ret;
  HwangsaeRelayAgent *self = HWANGSAE_RELAY_AGENT (app);

  g_debug ("hwangsae_relay_agent_dbus_register");

  /* chain up */
  ret =
      G_APPLICATION_CLASS (hwangsae_relay_agent_parent_class)->dbus_register
      (app, connection, object_path, error);

  if (ret &&
      !g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON
          (self->manager), connection,
          "/org/hwangsaeul/Hwangsae1/Manager", error)) {
    g_warning ("Failed to export Hwangsae1 D-Bus interface (reason: %s)",
        (*error)->message);
  }

  if (ret &&
      !g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON
          (self->edge_interface), connection,
          "/org/hwangsaeul/Hwangsae1/EdgeInterface", error)) {
    g_warning ("Failed to export Hwangsae1 D-Bus interface (reason: %s)",
        (*error)->message);
  }

  return ret;
}

static void
hwangsae_relay_agent_dbus_unregister (GApplication * app,
    GDBusConnection * connection, const gchar * object_path)
{
  HwangsaeRelayAgent *self = HWANGSAE_RELAY_AGENT (app);

  g_debug ("hwangsae_relay_agent_dbus_unregister");

  if (self->manager)
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON
        (self->manager));

  if (self->edge_interface)
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON
        (self->edge_interface));

  g_application_release (app);

  /* chain up */
  G_APPLICATION_CLASS (hwangsae_relay_agent_parent_class)->dbus_unregister (app,
      connection, object_path);
}

gboolean
hwangsae_relay_agent_edge_interface_handle_start (Hwangsae1DBusEdgeInterface *
    object, GDBusMethodInvocation * invocation, gchar * arg_id, gint arg_width,
    gint arg_height, gint arg_fps, gint arg_bitrates, gpointer user_data)
{
  ChamgeReturn ret = CHAMGE_RETURN_OK;
  g_autofree gchar *cmd = NULL;
  g_autofree gchar *response = NULL;
  GError *error = NULL;
  HwangsaeRelayAgent *self = (HwangsaeRelayAgent *) user_data;
  gchar *uid = NULL;
  const gchar *sink_uri = hwangsae_relay_get_sink_uri (self->relay);
  const gchar *source_uri = hwangsae_relay_get_source_uri (self->relay);

  cmd =
      g_strdup_printf ("{\"to\":\"%s\",\"method\":\"streamingStart\", "
      "\"params\": {\"url\": \"%s\", "
      "\"width\":%d, \"height\":%d, \"fps\": %d, \"bitrates\": %d}}",
      arg_id, sink_uri, arg_width, arg_height, arg_fps, arg_bitrates);

  g_debug ("hwangsae_relay_agent_edge_interface_handle_start, cmd %s", cmd);

  g_object_get (self->chamge_hub, "uid", &uid, NULL);
  g_assert_cmpstr (uid, ==, DEFAULT_HUB_UID);

  ret =
      chamge_node_user_command (CHAMGE_NODE (self->chamge_hub), cmd,
      &response, &error);

  if (ret != CHAMGE_RETURN_OK) {
    g_debug ("failed to send user command >> %s\n", error->message);
  }

  hwangsae1_dbus_edge_interface_complete_start (object, invocation, source_uri);

  return TRUE;
}

gboolean
hwangsae_relay_agent_edge_interface_handle_stop (Hwangsae1DBusEdgeInterface *
    object, GDBusMethodInvocation * invocation, gchar * arg_id,
    gpointer user_data)
{
  ChamgeReturn ret = CHAMGE_RETURN_OK;
  g_autofree gchar *cmd = NULL;
  g_autofree gchar *response = NULL;
  GError *error = NULL;
  HwangsaeRelayAgent *self = (HwangsaeRelayAgent *) user_data;
  gchar *uid = NULL;
  const gchar *uri = hwangsae_relay_get_sink_uri (self->relay);

  cmd =
      g_strdup_printf ("{\"to\":\"%s\",\"method\":\"streamingStop\"}", arg_id);

  g_debug ("hwangsae_relay_agent_edge_interface_handle_stop, cmd %s", cmd);

  g_object_get (self->chamge_hub, "uid", &uid, NULL);
  g_assert_cmpstr (uid, ==, DEFAULT_HUB_UID);

  ret =
      chamge_node_user_command (CHAMGE_NODE (self->chamge_hub), cmd,
      &response, &error);

  if (ret != CHAMGE_RETURN_OK) {
    g_debug ("failed to send user command >> %s\n", error->message);
  }

  hwangsae1_dbus_edge_interface_complete_stop (object, invocation, uri);

  return TRUE;
}

static gboolean
hwangsae_relay_agent_edge_interface_handle_change_parameters (
    Hwangsae1DBusEdgeInterface * object, GDBusMethodInvocation * invocation,
    gchar * arg_id, gint arg_width, gint arg_height, gint arg_fps,
    gint arg_bitrate, gpointer user_data)
{
  ChamgeReturn ret = CHAMGE_RETURN_OK;
  g_autofree gchar *cmd = NULL;
  g_autofree gchar *response = NULL;
  GError *error = NULL;
  HwangsaeRelayAgent *self = (HwangsaeRelayAgent *) user_data;

  cmd =
      g_strdup_printf
      ("{\"to\":\"%s\",\"method\":\"streamingChangeParameters\", "
      "\"params\": {\"width\":%d, \"height\":%d, \"fps\": %d, \"bitrates\": %d}}",
      arg_id, arg_width, arg_height, arg_fps, arg_bitrate);

  g_debug ("%s, cmd %s", __func__, cmd);

  ret =
      chamge_node_user_command (CHAMGE_NODE (self->chamge_hub), cmd,
      &response, &error);

  if (ret != CHAMGE_RETURN_OK) {
    g_debug ("failed to send user command >> %s\n", error->message);
  }

  hwangsae1_dbus_edge_interface_complete_change_parameters (object, invocation);

  return TRUE;
}

static void
hwangsae_relay_agent_dispose (GObject * object)
{
  HwangsaeRelayAgent *self = HWANGSAE_RELAY_AGENT (object);

  g_clear_object (&self->relay);

  g_clear_object (&self->manager);
  g_clear_object (&self->edge_interface);
  g_clear_object (&self->chamge_hub);

  G_OBJECT_CLASS (hwangsae_relay_agent_parent_class)->dispose (object);
}

static void
hwangsae_relay_agent_class_init (HwangsaeRelayAgentClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  gobject_class->dispose = hwangsae_relay_agent_dispose;

  app_class->dbus_register = hwangsae_relay_agent_dbus_register;
  app_class->dbus_unregister = hwangsae_relay_agent_dbus_unregister;
}

static gboolean
signal_handler (GApplication * app)
{
  g_application_release (app);

  return G_SOURCE_REMOVE;
}

static void
hwangsae_relay_agent_init (HwangsaeRelayAgent * self)
{
  ChamgeReturn ret;
  gchar *uid = NULL;

  self->relay = hwangsae_relay_new ();

  /* TODO : HUB UID should be get from configuration */
  self->chamge_hub = chamge_hub_new_full (DEFAULT_HUB_UID, DEFAULT_BACKEND);

  g_object_get (self->chamge_hub, "uid", &uid, NULL);
  g_assert_cmpstr (uid, ==, DEFAULT_HUB_UID);

  ret = chamge_node_enroll (CHAMGE_NODE (self->chamge_hub), FALSE);
  g_assert (ret == CHAMGE_RETURN_OK);

  ret = chamge_node_activate (CHAMGE_NODE (self->chamge_hub));
  g_assert (ret == CHAMGE_RETURN_OK);

  self->manager = hwangsae1_dbus_manager_skeleton_new ();

  hwangsae1_dbus_manager_set_status (self->manager, 1);

  self->edge_interface = hwangsae1_dbus_edge_interface_skeleton_new ();

  g_signal_connect (self->edge_interface, "handle-start",
      G_CALLBACK (hwangsae_relay_agent_edge_interface_handle_start), self);

  g_signal_connect (self->edge_interface, "handle-stop",
      G_CALLBACK (hwangsae_relay_agent_edge_interface_handle_stop), self);

  g_signal_connect (self->edge_interface, "handle-change-parameters",
      G_CALLBACK (hwangsae_relay_agent_edge_interface_handle_change_parameters),
      self);
}

int
main (int argc, char *argv[])
{
  g_autoptr (GApplication) app = NULL;

  app = G_APPLICATION (g_object_new (HWANGSAE_TYPE_RELAY_AGENT,
          "application-id", "org.hwangsaeul.Hwangsae1.RelayAgent",
          "flags", G_APPLICATION_IS_SERVICE, NULL));

  g_unix_signal_add (SIGINT, (GSourceFunc) signal_handler, app);

  g_application_hold (app);

  return hwangsaeul_application_run (HWANGSAEUL_APPLICATION (app), argc, argv);
}
