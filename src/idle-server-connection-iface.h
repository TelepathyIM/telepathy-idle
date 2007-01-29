/*
 * This file is part of telepathy-idle
 * 
 * Copyright (C) 2006 Nokia Corporation. All rights reserved.
 *
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

#ifndef __IDLE_SERVER_CONNECTION_IFACE_H__
#define __IDLE_SERVER_CONNECTION_IFACE_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define IDLE_TYPE_SERVER_CONNECTION_IFACE idle_server_connection_iface_get_type()

#define IDLE_SERVER_CONNECTION_IFACE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), \
								IDLE_TYPE_SERVER_CONNECTION_IFACE, IdleServerConnectionIface))

#define IDLE_SERVER_CONNECTION_IFACE_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((obj), \
							 IDLE_TYPE_SERVER_CONNECTION_IFACE, IdleServerConnectionIfaceClass))

#define IDLE_IS_SERVER_CONNECTION_IFACE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_SERVER_CONNECTION_IFACE))

#define IDLE_IS_SERVER_CONNECTION_IFACE_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_SERVER_CONNECTION_IFACE))

#define IDLE_SERVER_CONNECTION_IFACE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_INTERFACE((obj), \
	 IDLE_TYPE_SERVER_CONNECTION_IFACE, IdleServerConnectionIfaceClass))

typedef struct _IdleServerConnectionIface IdleServerConnectionIface;
typedef struct _IdleServerConnectionIfaceClass IdleServerConnectionIfaceClass;

typedef enum
{
	SERVER_CONNECTION_STATE_NOT_CONNECTED,
	SERVER_CONNECTION_STATE_CONNECTING,
	SERVER_CONNECTION_STATE_CONNECTED,
} IdleServerConnectionState;

typedef enum
{
	SERVER_CONNECTION_STATE_REASON_ERROR,
	SERVER_CONNECTION_STATE_REASON_REQUESTED
} IdleServerConnectionStateReason;

struct _IdleServerConnectionIfaceClass
{
	GTypeInterface parent_class;

	gboolean (*connect) (IdleServerConnectionIface *, GError **error);
	gboolean (*disconnect) (IdleServerConnectionIface *, GError **error);
	
	gboolean (*send) (IdleServerConnectionIface *, const gchar *cmd, GError **error);
	
	IdleServerConnectionState (*get_state) (IdleServerConnectionIface *);
};

GType idle_server_connection_iface_get_type(void);

gboolean idle_server_connection_iface_connect(IdleServerConnectionIface *, GError **error);
gboolean idle_server_connection_iface_disconnect(IdleServerConnectionIface *, GError **error);

gboolean idle_server_connection_iface_send(IdleServerConnectionIface *, const gchar *cmd, GError **error);

IdleServerConnectionState idle_server_connection_iface_get_state(IdleServerConnectionIface *);

G_END_DECLS

#endif
