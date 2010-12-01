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

#ifndef __IDLE_HANDLES_H__
#define __IDLE_HANDLES_H__

#include <glib.h>
#include <telepathy-glib/handle-repo.h>

G_BEGIN_DECLS

void idle_handle_repos_init(TpHandleRepoIface **handles);
gboolean idle_nickname_is_valid(const gchar *nickname, gboolean strict_mode);

gchar *idle_normalize_nickname (const gchar *nickname, GError **error);

G_END_DECLS

#endif /* __IDLE_HANDLES_H__ */
