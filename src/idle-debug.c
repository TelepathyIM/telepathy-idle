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

#include "idle-debug.h"

#include <glib.h>

#include <stdarg.h>

#include <telepathy-glib/debug.h>

static IdleDebugFlags _flags = 0;

static GDebugKey _keys[] = {
	{"connection", IDLE_DEBUG_CONNECTION},
	{"dns", IDLE_DEBUG_DNS},
	{"im", IDLE_DEBUG_IM},
	{"muc", IDLE_DEBUG_MUC},
	{"network", IDLE_DEBUG_NETWORK},
	{"parser", IDLE_DEBUG_PARSER},
	{"text", IDLE_DEBUG_TEXT},
	{NULL, 0}
};

void idle_debug_init() {
	guint nkeys;
	for (nkeys = 0; _keys[nkeys].value; nkeys++);

	const gchar *flags_string = g_getenv("IDLE_DEBUG");
	if (flags_string) {
		tp_debug_set_flags_from_env("IDLE_DEBUG");
		_flags |= g_parse_debug_string(flags_string, _keys, nkeys);
	}

	if (g_getenv("IDLE_PERSIST"))
		tp_debug_set_flags_from_string("persist");
}

void idle_debug(IdleDebugFlags flag, const gchar *format, ...) {
	if (!(_flags & flag))
		return;

	va_list args;
	va_start(args, format);
	g_logv(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format, args);
	va_end(args);
}

