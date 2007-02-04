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

#include <telepathy-glib/enums.h>
#include <telepathy-glib/handle-repo.h>

#include "idle-connection.h"

G_BEGIN_DECLS

gboolean idle_nickname_is_valid(const gchar *nickname);
gboolean idle_channelname_is_valid(const gchar *channelname);

const char *idle_handle_inspect(TpHandleRepoIface *storage, TpHandle handle);

TpHandle idle_handle_for_contact(TpHandleRepoIface *storage, const char *nickname);
TpHandle idle_handle_for_room(TpHandleRepoIface *storage, const char *channel);

gboolean idle_handle_set_presence(TpHandleRepoIface *storage, TpHandle contact_handle, IdleContactPresence *presence);
IdleContactPresence *idle_handle_get_presence(TpHandleRepoIface *storage, TpHandle contact_handle);

G_END_DECLS

#endif /* __IDLE_HANDLES_H__ */
