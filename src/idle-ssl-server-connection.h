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

#ifndef __IDLE_SSL_SERVER_CONNECTION_H__
#define __IDLE_SSL_SERVER_CONNECTION_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _IdleSSLServerConnection IdleSSLServerConnection;
typedef struct _IdleSSLServerConnectionClass IdleSSLServerConnectionClass;

struct _IdleSSLServerConnection
{
	GObject parent;
};

struct _IdleSSLServerConnectionClass
{
	GObjectClass parent;
};

GType idle_ssl_server_connection_get_type(void);

#define IDLE_TYPE_SSL_SERVER_CONNECTION \
	(idle_ssl_server_connection_get_type())

#define IDLE_SSL_SERVER_CONNECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), IDLE_TYPE_SSL_SERVER_CONNECTION, IdleSSLServerConnection))

#define IDLE_SSL_SERVER_CONNECTION_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), IDLE_TYPE_SSL_SERVER_CONNECTION, IdleSSLServerConnection))

#define IDLE_IS_SSL_SERVER_CONNECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_SSL_SERVER_CONNECTION))

#define IDLE_IS_SSL_SERVER_CONNECTION_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_SSL_SERVER_CONNECTION))

#define IDLE_SSL_SERVER_CONNECTION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS((obj), IDLE_TYPE_SSL_SERVER_CONNECTION, IdleSSLServerConnectionClass))

G_END_DECLS

#endif
