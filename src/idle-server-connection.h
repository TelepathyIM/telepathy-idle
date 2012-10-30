/*
 * This file is part of telepathy-idle
 *
 * Copyright (C) 2006-2007 Collabora Limited
 * Copyright (C) 2006-2007 Nokia Corporation
 * Copyright (C) 2011      Debarshi Ray <rishi@gnu.org>
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

#include <gio/gio.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _IdleServerConnection IdleServerConnection;
typedef struct _IdleServerConnectionClass IdleServerConnectionClass;

typedef enum {
	SERVER_CONNECTION_STATE_NOT_CONNECTED,
	SERVER_CONNECTION_STATE_CONNECTING,
	SERVER_CONNECTION_STATE_CONNECTED
} IdleServerConnectionState;

typedef enum {
	SERVER_CONNECTION_STATE_REASON_ERROR,
	SERVER_CONNECTION_STATE_REASON_REQUESTED
} IdleServerConnectionStateReason;

struct _IdleServerConnection {
	GObject parent;
};

struct _IdleServerConnectionClass {
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

void idle_server_connection_connect_async(IdleServerConnection *conn, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean idle_server_connection_connect_finish(IdleServerConnection *conn, GAsyncResult *result, GError **error);
void idle_server_connection_disconnect_async(IdleServerConnection *conn, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
void idle_server_connection_disconnect_full_async(IdleServerConnection *conn, guint reason, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
void idle_server_connection_force_disconnect(IdleServerConnection *conn);
gboolean idle_server_connection_disconnect_finish(IdleServerConnection *conn, GAsyncResult *result, GError **error);
void idle_server_connection_send_async(IdleServerConnection *conn, const gchar *cmd, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean idle_server_connection_send_finish(IdleServerConnection *conn, GAsyncResult *result, GError **error);
IdleServerConnectionState idle_server_connection_get_state(IdleServerConnection *conn);
void idle_server_connection_set_tls(IdleServerConnection *conn, gboolean tls);

G_END_DECLS

#endif
