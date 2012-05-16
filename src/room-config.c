/*
 * room-config.c - Channel.Interface.RoomConfig1 implementation
 * Copyright © 2011-2012 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "room-config.h"
#include "idle-muc-channel.h"

#include <telepathy-glib/telepathy-glib.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_MUC
#include "idle-debug.h"

static void idle_room_config_update_configuration_async (
    TpBaseRoomConfig *base_config,
    GHashTable *validated_properties,
    GAsyncReadyCallback callback,
    gpointer user_data);

struct _IdleRoomConfigPrivate {
    gpointer hi_dere;
};

G_DEFINE_TYPE (IdleRoomConfig, idle_room_config, TP_TYPE_BASE_ROOM_CONFIG)

static void
idle_room_config_password_protected_cb (GObject *object,
    GParamSpec *pspec,
    gpointer user_data)
{
  gboolean protected;

  g_object_get (object,
      "password-protected", &protected,
      NULL);

  if (!protected)
    {
      g_object_set (object,
          "password", "",
          NULL);
    }
}

static void
idle_room_config_init (IdleRoomConfig *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, IDLE_TYPE_ROOM_CONFIG,
      IdleRoomConfigPrivate);
}

static void
idle_room_config_constructed (GObject *object)
{
  TpBaseRoomConfig *self = TP_BASE_ROOM_CONFIG (object);
  static const TpBaseRoomConfigProperty prop_helper[] = {
    TP_BASE_ROOM_CONFIG_INVITE_ONLY,
    TP_BASE_ROOM_CONFIG_LIMIT,
    TP_BASE_ROOM_CONFIG_MODERATED,
    TP_BASE_ROOM_CONFIG_PASSWORD,
    TP_BASE_ROOM_CONFIG_PASSWORD_PROTECTED,
    TP_BASE_ROOM_CONFIG_PRIVATE,
  };
  guint i;
  void (*chain_up)(GObject *) =
      G_OBJECT_CLASS (idle_room_config_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (object);

  for (i = 0; i < G_N_ELEMENTS (prop_helper); ++i)
    {
      tp_base_room_config_set_property_mutable (self, prop_helper[i], TRUE);
    }

  /* Just to get the mutable properties out there. */
  tp_base_room_config_emit_properties_changed (self);

  /* Let's reset the password back to the empty string when
   * password-protected is set to False. */
  g_signal_connect (self, "notify::password-protected",
      G_CALLBACK (idle_room_config_password_protected_cb), NULL);
}

static void
idle_room_config_class_init (IdleRoomConfigClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  TpBaseRoomConfigClass *parent_class = TP_BASE_ROOM_CONFIG_CLASS (klass);

  object_class->constructed = idle_room_config_constructed;

  parent_class->update_async = idle_room_config_update_configuration_async;
  g_type_class_add_private (klass, sizeof (IdleRoomConfigPrivate));
}

IdleRoomConfig *
idle_room_config_new (
    TpBaseChannel *channel)
{
  g_return_val_if_fail (TP_IS_BASE_CHANNEL (channel), NULL);

  return g_object_new (IDLE_TYPE_ROOM_CONFIG,
      "channel", channel,
      NULL);
}

static void
send_mode (IdleRoomConfig *self,
    const gchar *cmd)
{
  TpBaseChannel *channel;
  TpBaseConnection *connection;
  gchar *s, *target_id;

  channel = tp_base_room_config_dup_channel ((TpBaseRoomConfig *) self);
  g_object_get (channel,
      "target-id", &target_id,
      NULL);

  connection = tp_base_channel_get_connection (channel);

  s = g_strdup_printf ("MODE %s %s", target_id, cmd);
  idle_connection_send (IDLE_CONNECTION (connection), s);
  g_free (s);

  g_free (target_id);
  g_object_unref (channel);
}

static void
do_quick_boolean (IdleRoomConfig *self,
    GHashTable *properties,
    TpBaseRoomConfigProperty prop_id,
    const gchar *gobject_property_name,
    const gchar irc_mode)
{
  if (g_hash_table_lookup (properties, GUINT_TO_POINTER (prop_id)) != NULL)
    {
      gboolean new_value = tp_asv_get_boolean (properties,
          GUINT_TO_POINTER (prop_id), NULL);
      gboolean current_value;

      g_object_get (self,
          gobject_property_name, &current_value,
          NULL);

      if (current_value != new_value)
        {
          gchar *cmd = g_strdup_printf ("%c%c",
              new_value ? '+' : '-', irc_mode);
          send_mode (self, cmd);
          g_free (cmd);
        }
    }
}

static void
idle_room_config_update_configuration_async (
    TpBaseRoomConfig *base_config,
    GHashTable *validated_properties,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  IdleRoomConfig *self = IDLE_ROOM_CONFIG (base_config);
  GSimpleAsyncResult *result = g_simple_async_result_new ((GObject *) self,
      callback, user_data, idle_room_config_update_configuration_async);
  gboolean present = FALSE;
  gboolean password_protected = FALSE;
  const gchar *password = NULL;

  password_protected = tp_asv_get_boolean (validated_properties,
      GUINT_TO_POINTER (TP_BASE_ROOM_CONFIG_PASSWORD_PROTECTED), &present);
  password = tp_asv_get_string (validated_properties,
      GUINT_TO_POINTER (TP_BASE_ROOM_CONFIG_PASSWORD));

  /* first, do sanity checking for Password & PasswordProtected */
  if (password_protected && tp_str_empty (password))
    {
      g_simple_async_result_set_error (result, TP_ERROR,
          TP_ERROR_INVALID_ARGUMENT,
          "PasswordProtected=True but no password given");
      g_simple_async_result_complete_in_idle (result);
      g_object_unref (result);
      return;
    }

  /* TODO: We shouldn't return from this function before we've had our
   * commands acked back to us from the server, but that's harder and
   * we'd have to add some queueing code which is a faff. We should
   * really do this though...
   *
   * http://i.imgur.com/FIOwY.jpg
   */

  if (!password_protected && present && password != NULL)
    {
      g_simple_async_result_set_error (result, TP_ERROR,
          TP_ERROR_INVALID_ARGUMENT,
          "PasswordProtected=False but then a password given, madness!");
      g_simple_async_result_complete_in_idle (result);
      g_object_unref (result);
      return;
    }

  /* okay go and do the quick ones */
  do_quick_boolean (self, validated_properties,
      TP_BASE_ROOM_CONFIG_INVITE_ONLY, "invite-only", 'i');
  do_quick_boolean (self, validated_properties,
      TP_BASE_ROOM_CONFIG_MODERATED, "moderated", 'm');
  do_quick_boolean (self, validated_properties,
      TP_BASE_ROOM_CONFIG_PRIVATE, "private", 's');

  /* now the rest */

  /* limit */
  if (g_hash_table_lookup (validated_properties,
          GUINT_TO_POINTER (TP_BASE_ROOM_CONFIG_LIMIT)) != NULL)
    {
      guint limit = tp_asv_get_uint32 (validated_properties,
          GUINT_TO_POINTER (TP_BASE_ROOM_CONFIG_LIMIT), NULL);
      gchar *cmd = NULL;
      guint current;

      g_object_get (self,
          "limit", &current,
          NULL);

      if (limit != current)
        {
          /* a non-zero limit means we want a limit enabled, so let's
           * set it. if the limit is zero let's disable the limit. */
          if (limit > 0)
            cmd = g_strdup_printf ("+l %u", limit);
          else
            cmd = g_strdup ("-l");
        }

      if (cmd != NULL)
        send_mode (self, cmd);
      g_free (cmd);
    }

  /* set a new password */
  if (password != NULL)
    {
      gchar *cmd;

      /* we've already validated this; either PasswordProtected was
       * not included, or it's TRUE */

      cmd = g_strdup_printf ("+k %s", password);
      send_mode (self, cmd);
      g_free (cmd);

      /* TODO: this can be removed when we add queueing to these mode
       * changes */
      g_object_set (self,
          "password-protected", TRUE,
          NULL);
    }

  /* unset a password */
  if (!password_protected && present)
    {
      gchar *cmd;

      /* we've already validated this; PasswordProtected=FALSE so no
       * Password is given */

      cmd = g_strdup ("-k");
      send_mode (self, cmd);
      g_free (cmd);
    }

  g_simple_async_result_complete_in_idle (result);
  g_object_unref (result);
}
