/*
 * This file is part of telepathy-idle
 *
 * Copyright (C) 2007 Collabora Limited
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

#ifndef __IDLE_DEBUG_H_
#define __IDLE_DEBUG_H_

#include <glib.h>

typedef enum {
	IDLE_DEBUG_CONNECTION = (1 << 0),
	IDLE_DEBUG_DNS = (1 << 1),
	IDLE_DEBUG_IM = (1 << 2),
	IDLE_DEBUG_MUC = (1 << 3),
	IDLE_DEBUG_NETWORK = (1 << 4),
	IDLE_DEBUG_PARSER = (1 << 5),
	IDLE_DEBUG_TEXT = (1 << 6),
	IDLE_DEBUG_ROOMLIST = (1 << 7),
} IdleDebugFlags;

void idle_debug_init (void);
void idle_debug(IdleDebugFlags flag, const gchar *format, ...) G_GNUC_PRINTF(2, 3);

void idle_debug_free (void);

#endif

#ifdef IDLE_DEBUG_FLAG

#undef IDLE_DEBUG
#define IDLE_DEBUG(format, ...) \
	idle_debug(IDLE_DEBUG_FLAG, "%s: " format, G_STRFUNC, ##__VA_ARGS__)

#endif

