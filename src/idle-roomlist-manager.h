/*
 * This file is part of telepathy-idle
 *
 * Copyright (C) 2009 Collabora Limited
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
 *
 * Authors:
 *  Jonathon Jongsma <jonathon.jongsma@collabora.co.uk>
 */

#ifndef __IDLE_ROOMLIST_MANAGER_H__
#define __IDLE_ROOMLIST_MANAGER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _IdleRoomlistManager IdleRoomlistManager;
typedef struct _IdleRoomlistManagerClass IdleRoomlistManagerClass;
typedef struct _IdleRoomlistManagerPrivate IdleRoomlistManagerPrivate;

struct _IdleRoomlistManagerClass {
        GObjectClass parent_class;
};

struct _IdleRoomlistManager {
        GObject parent;
        IdleRoomlistManagerPrivate *priv;
};

GType idle_roomlist_manager_get_type (void);

#define IDLE_TYPE_ROOMLIST_MANAGER (idle_roomlist_manager_get_type())
#define IDLE_ROOMLIST_MANAGER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), IDLE_TYPE_ROOMLIST_MANAGER, IdleRoomlistManager))
#define IDLE_ROOMLIST_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), IDLE_TYPE_ROOMLIST_MANAGER, IdleRoomlistManagerClass))
#define IDLE_IS_ROOMLIST_MANAGER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_ROOMLIST_MANAGER))
#define IDLE_IS_ROOMLIST_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_ROOMLIST_MANAGER))
#define IDLE_ROOMLIST_MANAGER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), IDLE_TYPE_ROOMLIST_MANAGER, IdleRoomlistManagerClass))

G_END_DECLS

#endif /* #ifndef __IDLE_ROOMLIST_MANAGER_H__ */

