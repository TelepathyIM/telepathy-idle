/*
 * protocol.c - IdleProtocol
 * Copyright © 2007-2010 Collabora Ltd.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#include "protocol.h"

#include <dbus/dbus-glib.h>
#include <dbus/dbus-protocol.h>
#include <telepathy-glib/base-connection-manager.h>
#include <telepathy-glib/telepathy-glib.h>

#include "idle-connection.h"
#include "idle-handles.h"
#include "idle-im-manager.h"
#include "idle-muc-manager.h"

#define PROTOCOL_NAME "irc"
#define ICON_NAME "im-" PROTOCOL_NAME
#define ENGLISH_NAME "IRC"
#define VCARD_FIELD_NAME "x-" PROTOCOL_NAME
#define DEFAULT_PORT 6667
#define DEFAULT_KEEPALIVE_INTERVAL 30 /* sec */

G_DEFINE_TYPE (IdleProtocol, idle_protocol, TP_TYPE_BASE_PROTOCOL)

static gboolean
filter_nick (const TpCMParamSpec *paramspec,
    GValue *value,
    GError **error)
{
  const gchar *nick = g_value_get_string (value);

  g_assert (value);
  g_assert (G_VALUE_HOLDS_STRING(value));

  if (!idle_nickname_is_valid (nick, TRUE))
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_HANDLE,
          "Invalid account name '%s'", nick);
      return FALSE;
    }

  return TRUE;
}

static gboolean
filter_username (const TpCMParamSpec *paramspec,
    GValue *value,
    GError **error)
{
  const gchar *username;
  size_t i;

  g_assert (value);
  g_assert (G_VALUE_HOLDS_STRING (value));

  username = g_value_get_string (value);

  for (i = 0; username[i] != '\0'; i++)
    {
      const char ch = username[i];

      if (ch == '\0' || ch == '\n' || ch == '\r' || ch == ' ' || ch == '@')
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "Invalid user name '%s'", username);
          return FALSE;
        }
    }

  return TRUE;
}

static const TpCMParamSpec idle_params[] = {
    {"account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL, 0, filter_nick},
    { "server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_REQUIRED },
    { "port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER (DEFAULT_PORT) },
    { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_SECRET },
    { "fullname", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0 },
    { "username", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, 0,
      filter_username },
    { "charset", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, "UTF-8" },
    { "keepalive-interval", DBUS_TYPE_UINT32_AS_STRING, G_TYPE_UINT,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
      GUINT_TO_POINTER (DEFAULT_KEEPALIVE_INTERVAL) },
    { "quit-message", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0 },
    { "use-ssl", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER (FALSE) },
    { "password-prompt", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER (FALSE) },
    { NULL, NULL, 0, 0, NULL, 0 }
};

static void
idle_protocol_init (IdleProtocol *self)
{
}

static const TpCMParamSpec *
get_parameters (TpBaseProtocol *self G_GNUC_UNUSED)
{
  return idle_params;
}

static TpBaseConnection *
new_connection (TpBaseProtocol *protocol G_GNUC_UNUSED,
                GHashTable *params,
                GError **error G_GNUC_UNUSED)
{
  guint port = tp_asv_get_uint32 (params, "port", NULL);

  if (port == 0)
    port = DEFAULT_PORT;

  return g_object_new (IDLE_TYPE_CONNECTION,
      "protocol", PROTOCOL_NAME,
      "nickname", tp_asv_get_string (params, "account"),
      "server", tp_asv_get_string (params, "server"),
      "port", port,
      "password", tp_asv_get_string (params, "password"),
      "realname", tp_asv_get_string (params, "fullname"),
      "username", tp_asv_get_string (params, "username"),
      "charset", tp_asv_get_string (params, "charset"),
      "keepalive-interval", tp_asv_get_uint32 (params, "keepalive-interval", NULL),
      "quit-message", tp_asv_get_string (params, "quit-message"),
      "use-ssl", tp_asv_get_boolean (params, "use-ssl", NULL),
      "password-prompt", tp_asv_get_boolean (params, "password-prompt",
          NULL),
      NULL);
}

static gchar *
normalize_contact (TpBaseProtocol *self G_GNUC_UNUSED,
                   const gchar *contact,
                   GError **error)
{
  return idle_normalize_nickname (contact, error);
}

static gchar *
identify_account (TpBaseProtocol *self G_GNUC_UNUSED,
    GHashTable *asv,
    GError **error)
{
  gchar *nick = idle_normalize_nickname (tp_asv_get_string (asv, "account"),
      error);
  gchar *server;
  gchar *nick_at_server;

  if (nick == NULL)
    return NULL;

  server = g_ascii_strdown (tp_asv_get_string (asv, "server"), -1);

  nick_at_server = g_strdup_printf ("%s@%s", nick, server);
  g_free (server);
  g_free (nick);
  return nick_at_server;
}

static GStrv
get_interfaces (TpBaseProtocol *self)
{
  return g_new0 (gchar *, 1);
}

static void
get_connection_details (TpBaseProtocol *self,
    GStrv *connection_interfaces,
    GType **channel_managers,
    gchar **icon_name,
    gchar **english_name,
    gchar **vcard_field)
{
  if (connection_interfaces != NULL)
    {
      *connection_interfaces = g_strdupv (
          (GStrv) idle_connection_get_implemented_interfaces ());
    }

  if (channel_managers != NULL)
    {
      GType types[] = {
          IDLE_TYPE_IM_MANAGER,
          IDLE_TYPE_MUC_MANAGER,
          G_TYPE_INVALID };

      *channel_managers = g_memdup (types, sizeof(types));
    }

  if (icon_name != NULL)
    {
      *icon_name = g_strdup (ICON_NAME);
    }

  if (vcard_field != NULL)
    {
      *vcard_field = g_strdup (VCARD_FIELD_NAME);
    }

  if (english_name != NULL)
    {
      *english_name = g_strdup (ENGLISH_NAME);
    }
}

static GStrv
dup_authentication_types (TpBaseProtocol *base)
{
  const gchar * const types[] = {
    TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
    NULL,
  };

  return g_strdupv ((GStrv) types);
}

static void
idle_protocol_class_init (IdleProtocolClass *klass)
{
  TpBaseProtocolClass *base_class = (TpBaseProtocolClass *) klass;

  base_class->get_parameters = get_parameters;
  base_class->new_connection = new_connection;
  base_class->normalize_contact = normalize_contact;
  base_class->identify_account = identify_account;
  base_class->get_interfaces = get_interfaces;
  base_class->get_connection_details = get_connection_details;
  base_class->dup_authentication_types = dup_authentication_types;
}

TpBaseProtocol *
idle_protocol_new (void)
{
  return g_object_new (IDLE_TYPE_PROTOCOL,
      "name", PROTOCOL_NAME,
      NULL);
}
