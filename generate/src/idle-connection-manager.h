/*
 * idle-connection-manager.h - Header for IdleConnectionManager
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

#ifndef __IDLE_CONNECTION_MANAGER_H__
#define __IDLE_CONNECTION_MANAGER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _IdleConnectionManager IdleConnectionManager;
typedef struct _IdleConnectionManagerClass IdleConnectionManagerClass;

struct _IdleConnectionManagerClass {
    GObjectClass parent_class;
};

struct _IdleConnectionManager {
    GObject parent;
};

GType idle_connection_manager_get_type(void);

/* TYPE MACROS */
#define IDLE_TYPE_CONNECTION_MANAGER \
  (idle_connection_manager_get_type())
#define IDLE_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), IDLE_TYPE_CONNECTION_MANAGER, IdleConnectionManager))
#define IDLE_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), IDLE_TYPE_CONNECTION_MANAGER, IdleConnectionManagerClass))
#define IDLE_IS_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_CONNECTION_MANAGER))
#define IDLE_IS_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_CONNECTION_MANAGER))
#define IDLE_CONNECTION_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), IDLE_TYPE_CONNECTION_MANAGER, IdleConnectionManagerClass))


gboolean idle_connection_manager_connect (IdleConnectionManager *obj, const gchar * proto, GHashTable * parameters, gchar ** ret, gchar ** ret1, GError **error);
gboolean idle_connection_manager_get_mandatory_parameters (IdleConnectionManager *obj, const gchar * proto, GHashTable ** ret, GError **error);
gboolean idle_connection_manager_get_optional_parameters (IdleConnectionManager *obj, const gchar * proto, GHashTable ** ret, GError **error);
gboolean idle_connection_manager_get_parameter_defaults (IdleConnectionManager *obj, const gchar * proto, GHashTable ** ret, GError **error);
gboolean idle_connection_manager_list_protocols (IdleConnectionManager *obj, gchar *** ret, GError **error);


G_END_DECLS

#endif /* #ifndef __IDLE_CONNECTION_MANAGER_H__*/
