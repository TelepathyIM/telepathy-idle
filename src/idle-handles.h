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

#ifndef __IDLE_HANDLES_H__
#define __IDLE_HANDLES_H__

#include <glib.h>

typedef struct _IdleHandleStorage IdleHandleStorage;
typedef guint IdleHandle;

#include "idle-connection.h"
#include "telepathy-constants.h"

G_BEGIN_DECLS

gboolean idle_handle_type_is_valid(TpHandleType type);

IdleHandleStorage *idle_handle_storage_new();
void idle_handle_storage_destroy(IdleHandleStorage *storage);

gboolean idle_nickname_is_valid(const gchar *nickname);
gboolean idle_channelname_is_valid(const gchar *channelname);

gboolean idle_handle_is_valid(IdleHandleStorage *storage, TpHandleType type, IdleHandle handle);
gboolean idle_handle_ref(IdleHandleStorage *storage, TpHandleType type, IdleHandle handle);
gboolean idle_handle_unref(IdleHandleStorage *storage, TpHandleType type, IdleHandle handle);
const char *idle_handle_inspect(IdleHandleStorage *storage, TpHandleType type, IdleHandle handle);

IdleHandle idle_handle_for_contact(IdleHandleStorage *storage, const char *nickname);
gboolean idle_handle_for_room_exists(IdleHandleStorage *storage, const char *channel);
IdleHandle idle_handle_for_room(IdleHandleStorage *storage, const char *channel);

gboolean idle_handle_set_presence(IdleHandleStorage *storage, IdleHandle contact_handle, IdleContactPresence *presence);
IdleContactPresence *idle_handle_get_presence(IdleHandleStorage *storage, IdleHandle contact_handle);

G_END_DECLS

#endif /* __IDLE_HANDLES_H__ */
