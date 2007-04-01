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

#ifndef __IDLE_CONNECTION_H__
#define __IDLE_CONNECTION_H__

#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/handle-repo.h>

typedef struct _IdleConnection IdleConnection;
typedef struct _IdleConnectionClass IdleConnectionClass;
typedef struct _IdleContactPresence IdleContactPresence;

#include "idle-handles.h"
#include "idle-parser.h"

#define IRC_MSG_MAXLEN 510

G_BEGIN_DECLS

typedef enum
{
	IDLE_PRESENCE_AVAILABLE,
	IDLE_PRESENCE_AWAY,
	IDLE_PRESENCE_OFFLINE,
	LAST_IDLE_PRESENCE_ENUM,
} IdlePresenceState;

struct _IdleConnectionClass 
{
    GObjectClass parent_class;
};

struct _IdleConnection 
{
    GObject parent;
    TpHandleRepoIface *handles[LAST_TP_HANDLE_TYPE + 1];
		IdleParser *parser;
};

void idle_contact_presence_free(IdleContactPresence *cp);

GType idle_connection_get_type(void);

/* TYPE MACROS */
#define IDLE_TYPE_CONNECTION \
  (idle_connection_get_type())
#define IDLE_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), IDLE_TYPE_CONNECTION, IdleConnection))
#define IDLE_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), IDLE_TYPE_CONNECTION, IdleConnectionClass))
#define IDLE_IS_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_CONNECTION))
#define IDLE_IS_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_CONNECTION))
#define IDLE_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), IDLE_TYPE_CONNECTION, IdleConnectionClass))

gboolean _idle_connection_register(IdleConnection *conn, char **bus_name, char **object_path, GError **error);
gboolean _idle_connection_send(IdleConnection *conn, const gchar *msg);
void _idle_connection_client_hold_handle(IdleConnection *conn, gchar *client_name, TpHandle handle, TpHandleType type);
gboolean _idle_connection_client_release_handle(IdleConnection *conn, gchar *client_name, TpHandle handle, TpHandleType type);

gboolean idle_connection_add_status (IdleConnection *obj, const gchar *status, GHashTable *parms, GError **error);
gboolean idle_connection_clear_status (IdleConnection *obj, GError **error);
gboolean idle_connection_connect (IdleConnection *obj, GError **error);
gboolean idle_connection_disconnect (IdleConnection *obj, GError **error);
gboolean idle_connection_get_interfaces (IdleConnection *obj, gchar ***ret, GError **error);
gboolean idle_connection_get_protocol (IdleConnection *obj, gchar **ret, GError **error);
gboolean idle_connection_get_self_handle (IdleConnection *obj, guint*ret, GError **error);
gboolean idle_connection_get_status (IdleConnection *obj, guint*ret, GError **error);
gboolean idle_connection_get_statuses (IdleConnection *obj, GHashTable **ret, GError **error);
gboolean idle_connection_hold_handles (IdleConnection *obj, guint handle_type, const GArray *handles, DBusGMethodInvocation *context);
gboolean idle_connection_inspect_handles (IdleConnection *obj, guint handle_type, const GArray *handles, DBusGMethodInvocation *context);
gboolean idle_connection_list_channels (IdleConnection *obj, GPtrArray **ret, GError **error);
gboolean idle_connection_release_handles (IdleConnection *obj, guint handle_type, const GArray *handles, DBusGMethodInvocation *context);
gboolean idle_connection_remove_status (IdleConnection *obj, const gchar *status, GError **error);
gboolean idle_connection_request_channel (IdleConnection *obj, const gchar *type, guint handle_type, guint handle, gboolean suppress_handler, DBusGMethodInvocation *ctx);
gboolean idle_connection_request_handles (IdleConnection *obj, guint handle_type, const gchar **names, DBusGMethodInvocation *context);
gboolean idle_connection_request_presence (IdleConnection *obj, const GArray *contacts, GError **error);
gboolean idle_connection_set_last_activity_time (IdleConnection *obj, guint time, GError **error);
gboolean idle_connection_set_status (IdleConnection *obj, GHashTable *statuses, GError **error);

gboolean idle_connection_hton(IdleConnection *obj, const gchar *input, gchar **output, GError **error);
gboolean idle_connection_ntoh(IdleConnection *obj, const gchar *input, gchar **output, GError **error);

G_END_DECLS

#endif /* #ifndef __IDLE_CONNECTION_H__*/
