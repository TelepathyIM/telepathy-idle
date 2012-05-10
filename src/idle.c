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

#include <config.h>

#include <telepathy-glib/run.h>
#include <telepathy-glib/debug.h>
#include <telepathy-glib/debug-sender.h>

#include "idle-connection-manager.h"
#include "idle-debug.h"

static TpBaseConnectionManager *_construct_cm (void) {
	TpBaseConnectionManager *base_cm = TP_BASE_CONNECTION_MANAGER(g_object_new(IDLE_TYPE_CONNECTION_MANAGER, NULL));

	return base_cm;
}

int main(int argc, char **argv) {
	TpDebugSender *debug_sender;
	int result;

	g_type_init ();
	tp_debug_divert_messages (g_getenv ("IDLE_LOGFILE"));

	idle_debug_init();

	debug_sender = tp_debug_sender_dup ();

	result = tp_run_connection_manager("telepathy-idle", VERSION, _construct_cm, argc, argv);

	g_object_unref (debug_sender);
	return result;
}
