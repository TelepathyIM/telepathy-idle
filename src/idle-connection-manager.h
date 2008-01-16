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

#ifndef __IDLE_CONNECTION_MANAGER_H__
#define __IDLE_CONNECTION_MANAGER_H__

#include <glib-object.h>
#include <telepathy-glib/base-connection-manager.h>

G_BEGIN_DECLS

typedef struct _IdleConnectionManager IdleConnectionManager;
typedef struct _IdleConnectionManagerClass IdleConnectionManagerClass;

struct _IdleConnectionManagerClass {
	TpBaseConnectionManagerClass parent_class;
};

struct _IdleConnectionManager {
	TpBaseConnection parent;
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
	(G_TYPE_INSTANCE_GET_CLASS((obj), IDLE_TYPE_CONNECTION_MANAGER, IdleConnectionManagerClass))

G_END_DECLS

#endif /* #ifndef __IDLE_CONNECTION_MANAGER_H__*/
