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

#include "idle-server-connection-iface.h"

#include "idle-server-connection-iface-signals-marshal.h"

static void idle_server_connection_iface_base_init(gpointer klass) {
	static gboolean initialized = FALSE;

	if (!initialized) {
		initialized = TRUE;

		g_signal_new("status-changed",
					 G_OBJECT_CLASS_TYPE(klass),
					 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
					 0,
					 NULL, NULL,
					 idle_server_connection_iface_marshal_VOID__UINT_UINT,
					 G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

		g_signal_new("received",
				     G_OBJECT_CLASS_TYPE(klass),
					 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
					 0,
					 NULL, NULL,
					 g_cclosure_marshal_VOID__STRING,
					 G_TYPE_NONE, 1, G_TYPE_STRING);
	}
}

GType idle_server_connection_iface_get_type(void) {
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof (IdleServerConnectionIfaceClass),
			idle_server_connection_iface_base_init,
			NULL,
			NULL,
			NULL,
			NULL,
			0,
			0,
			NULL,
			NULL
		};

		type = g_type_register_static(G_TYPE_INTERFACE, "IdleServerConnectionIface", &info, 0);
	}

	return type;
}

gboolean idle_server_connection_iface_connect(IdleServerConnectionIface *iface, GError **error) {
	return IDLE_SERVER_CONNECTION_IFACE_GET_CLASS(iface)->connect(iface, error);
}

gboolean idle_server_connection_iface_disconnect(IdleServerConnectionIface *iface, GError **error) {
	return IDLE_SERVER_CONNECTION_IFACE_GET_CLASS(iface)->disconnect(iface, error);
}

gboolean idle_server_connection_iface_send(IdleServerConnectionIface *iface, const gchar *cmd, GError **error) {
	return IDLE_SERVER_CONNECTION_IFACE_GET_CLASS(iface)->send(iface, cmd, error);
}

IdleServerConnectionState idle_server_connection_iface_get_state(IdleServerConnectionIface *iface) {
	return IDLE_SERVER_CONNECTION_IFACE_GET_CLASS(iface)->get_state(iface);
}

