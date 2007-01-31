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

#ifndef __IDLE_HANDLE_SET_H__
#define __IDLE_HANDLE_SET_H__

#include <glib.h>
#include <telepathy-glib/intset.h>

#include "idle-handles.h"
#include "telepathy-constants.h"

G_BEGIN_DECLS

typedef struct _IdleHandleSet IdleHandleSet;
typedef void (*IdleHandleFunc)(IdleHandleSet *set, IdleHandle handle, gpointer userdata);

IdleHandleSet *idle_handle_set_new(IdleHandleStorage *storage, TpHandleType type);
void idle_handle_set_destroy(IdleHandleSet *set);

void idle_handle_set_add(IdleHandleSet *set, IdleHandle handle);
gboolean idle_handle_set_remove(IdleHandleSet *set, IdleHandle handle);
gboolean idle_handle_set_contains(IdleHandleSet *set, IdleHandle handle);

void idle_handle_set_foreach(IdleHandleSet *set, IdleHandleFunc func, gpointer userdata);

gint idle_handle_set_size(IdleHandleSet *set);
GArray *idle_handle_set_to_array(IdleHandleSet *set);

TpIntSet *idle_handle_set_update(IdleHandleSet *set, const TpIntSet *add);
TpIntSet *idle_handle_set_difference_update(IdleHandleSet *set, const TpIntSet *remove);

#endif /* __IDLE_HANDLE_SET_H__ */
