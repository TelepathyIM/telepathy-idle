/*
 * idle-connection.h - Header for IdleConnection
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#ifndef __IDLE_CONNECTION_H__
#define __IDLE_CONNECTION_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _IdleConnection IdleConnection;
typedef struct _IdleConnectionClass IdleConnectionClass;

struct _IdleConnectionClass {
    GObjectClass parent_class;
};

struct _IdleConnection {
    GObject parent;
};

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


gboolean idle_connection_add_status (IdleConnection *obj, const gchar * status, GHashTable * parms, GError **error);
gboolean idle_connection_advertise_capabilities (IdleConnection *obj, const gchar ** add, const gchar ** remove, GError **error);
gboolean idle_connection_clear_status (IdleConnection *obj, GError **error);
gboolean idle_connection_disconnect (IdleConnection *obj, GError **error);
gboolean idle_connection_get_capabilities (IdleConnection *obj, guint handle, GPtrArray ** ret, GError **error);
gboolean idle_connection_get_interfaces (IdleConnection *obj, gchar *** ret, GError **error);
gboolean idle_connection_get_protocol (IdleConnection *obj, gchar ** ret, GError **error);
gboolean idle_connection_get_self_handle (IdleConnection *obj, guint* ret, GError **error);
gboolean idle_connection_get_status (IdleConnection *obj, guint* ret, GError **error);
gboolean idle_connection_get_statuses (IdleConnection *obj, GHashTable ** ret, GError **error);
gboolean idle_connection_hold_handle (IdleConnection *obj, guint handle_type, guint handle, DBusGMethodInvocation *context);
gboolean idle_connection_inspect_handle (IdleConnection *obj, guint handle_type, guint handle, gchar ** ret, GError **error);
gboolean idle_connection_list_channels (IdleConnection *obj, GPtrArray ** ret, GError **error);
gboolean idle_connection_release_handle (IdleConnection *obj, guint handle_type, guint handle, DBusGMethodInvocation *context);
gboolean idle_connection_remove_status (IdleConnection *obj, const gchar * status, GError **error);
gboolean idle_connection_request_channel (IdleConnection *obj, const gchar * type, guint handle_type, guint handle, gboolean suppress_handler, gchar ** ret, GError **error);
gboolean idle_connection_request_handle (IdleConnection *obj, guint handle_type, const gchar * name, DBusGMethodInvocation *context);
gboolean idle_connection_request_presence (IdleConnection *obj, const GArray * contacts, GError **error);
gboolean idle_connection_request_rename (IdleConnection *obj, const gchar * name, GError **error);
gboolean idle_connection_set_last_activity_time (IdleConnection *obj, guint time, GError **error);
gboolean idle_connection_set_status (IdleConnection *obj, GHashTable * statuses, GError **error);


G_END_DECLS

#endif /* #ifndef __IDLE_CONNECTION_H__*/
