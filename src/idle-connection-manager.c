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

#include "config.h"

#include "idle-connection-manager.h"

#include <dbus/dbus-protocol.h>
#include <telepathy-glib/enums.h>

#include "idle-connection.h"
#include "idle-handles.h" /* to check for valid nick */
#include "idle-debug.h"
#include "protocol.h"

G_DEFINE_TYPE(IdleConnectionManager, idle_connection_manager, TP_TYPE_BASE_CONNECTION_MANAGER)

static void idle_connection_manager_init(IdleConnectionManager *obj) {
}

static void
idle_connection_manager_finalize (GObject *object)
{
	idle_debug_free ();

	G_OBJECT_CLASS (idle_connection_manager_parent_class)->finalize (object);
}

static void idle_connection_manager_constructed (GObject *object) {
	TpBaseConnectionManager *base = (TpBaseConnectionManager *) object;
	TpBaseProtocol *p;
	void (*constructed) (GObject *) = ((GObjectClass *) idle_connection_manager_parent_class)->constructed;

	if (constructed != NULL)
		constructed (object);

	p = idle_protocol_new ();
	tp_base_connection_manager_add_protocol (base, p);
	g_object_unref (p);
}

static void idle_connection_manager_class_init(IdleConnectionManagerClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	TpBaseConnectionManagerClass *parent_class = TP_BASE_CONNECTION_MANAGER_CLASS(klass);

	parent_class->cm_dbus_name = "idle";

	object_class->finalize = idle_connection_manager_finalize;
	object_class->constructed = idle_connection_manager_constructed;
}
