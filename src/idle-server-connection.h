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

#ifndef __IDLE_SERVER_CONNECTION_H__
#define __IDLE_SERVER_CONNECTION_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _IdleServerConnection IdleServerConnection;
typedef struct _IdleServerConnectionClass IdleServerConnectionClass;

struct _IdleServerConnection
{
	GObject parent;
};

struct _IdleServerConnectionClass
{
	GObjectClass parent;
};

GType idle_server_connection_get_type(void);

#define IDLE_TYPE_SERVER_CONNECTION \
	(idle_server_connection_get_type())

#define IDLE_SERVER_CONNECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), IDLE_TYPE_SERVER_CONNECTION, IdleServerConnection))

#define IDLE_SERVER_CONNECTION_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), IDLE_TYPE_SERVER_CONNECTION, IdleServerConnection))

#define IDLE_IS_SERVER_CONNECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_SERVER_CONNECTION))

#define IDLE_IS_SERVER_CONNECTION_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_SERVER_CONNECTION))

#define IDLE_SERVER_CONNECTION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS((obj), IDLE_TYPE_SERVER_CONNECTION, IdleServerConnectionClass))

G_END_DECLS

#endif
