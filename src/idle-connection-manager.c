/*
 * This file is part of telepathy-idle
 *
 * Copyright (C) 2006-2007 Collabora Limited
 * Copyright (C) 2006-2007 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
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

#include "idle-connection-manager.h"

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-protocol.h>
#include <telepathy-glib/enums.h>

#include "idle-connection.h"
#include "idle-handles.h" /* to check for valid nick */
#include "idle-debug.h"

G_DEFINE_TYPE(IdleConnectionManager, idle_connection_manager, TP_TYPE_BASE_CONNECTION_MANAGER)

typedef struct _Params Params;
struct _Params {
	gchar *account;
	gchar *server;
	guint16 port;
	gchar *password;
	gchar *fullname;
	gchar *username;
	gchar *charset;
	gchar *quit_message;
	gboolean use_ssl;
};

static gpointer _params_new() {
	Params *params = g_slice_new0(Params);

	return params;
}

static void _params_free(gpointer ptr) {
	Params *params = (Params *) ptr;

	g_free(params->account);
	g_free(params->server);
	g_free(params->password);
	g_free(params->fullname);
	g_free(params->username);
	g_free(params->charset);
	g_free(params->quit_message);

	g_slice_free(Params, params);
}

gboolean
filter_nick(const TpCMParamSpec *paramspec, GValue *value, GError **error)
{
	g_assert(value);
	g_assert(G_VALUE_HOLDS_STRING(value));

	const gchar* nick = g_value_get_string (value);
	if (!idle_nickname_is_valid(nick, TRUE)) {
		g_set_error(error, TP_ERRORS, TP_ERROR_INVALID_HANDLE, "Invalid account name '%s'", nick);
		return FALSE;
	}

	return TRUE;
}

static const TpCMParamSpec _params[] = {
	{"account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL, G_STRUCT_OFFSET(Params, account), filter_nick},
	{"server",  DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL, G_STRUCT_OFFSET(Params, server)},
	{"port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(6667), G_STRUCT_OFFSET(Params, port)},
	{"password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, G_STRUCT_OFFSET(Params, password)},
	{"fullname", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, G_STRUCT_OFFSET(Params, fullname)},
	{"username", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, G_STRUCT_OFFSET(Params, username)},
	{"charset", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, "UTF-8", G_STRUCT_OFFSET(Params, charset)},
	{"quit-message", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, G_STRUCT_OFFSET(Params, quit_message)},
	{"use-ssl", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE), G_STRUCT_OFFSET(Params, use_ssl)},
	{NULL, NULL, 0, 0, NULL, 0}
};

static const TpCMProtocolSpec _protocols[] = {
	{"irc", _params, _params_new, _params_free},
	{NULL, NULL, NULL, NULL}
};

static TpBaseConnection *_iface_new_connection(TpBaseConnectionManager *self, const gchar *proto, TpIntSet *params_present, void *parsed_params, GError **error);

static void idle_connection_manager_init(IdleConnectionManager *obj) {
}

static void
idle_connection_manager_finalize (GObject *object)
{
	idle_debug_free ();

	G_OBJECT_CLASS (idle_connection_manager_parent_class)->finalize (object);
}

static void idle_connection_manager_class_init(IdleConnectionManagerClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	TpBaseConnectionManagerClass *parent_class = TP_BASE_CONNECTION_MANAGER_CLASS(klass);

	parent_class->new_connection = _iface_new_connection;
	parent_class->cm_dbus_name = "idle";
	parent_class->protocol_params = _protocols;

	object_class->finalize = idle_connection_manager_finalize;
}

static TpBaseConnection *_iface_new_connection(TpBaseConnectionManager *self, const gchar *proto, TpIntSet *params_present, void *parsed_params, GError **error) {
	IdleConnection *conn;
	Params *params = (Params *) parsed_params;

	g_assert(IDLE_IS_CONNECTION_MANAGER(self));

	conn = g_object_new(IDLE_TYPE_CONNECTION,
			"protocol", proto,
			"nickname", params->account,
			"server", params->server,
			"port", params->port,
			"password", params->password,
			"realname", params->fullname,
			"username", params->username,
			"charset", params->charset,
			"quit-message", params->quit_message,
			"use-ssl", params->use_ssl,
			NULL);

	return TP_BASE_CONNECTION(conn);
}

