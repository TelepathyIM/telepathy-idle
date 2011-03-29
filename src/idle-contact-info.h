/*
 * This file is part of telepathy-idle
 *
 * Copyright (C) 2011 Debarshi Ray <rishi@gnu.org>
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

#ifndef __IDLE_CONTACT_INFO_H__
#define __IDLE_CONTACT_INFO_H__

#include <glib.h>
#include <glib-object.h>
#include <telepathy-glib/dbus-properties-mixin.h>

#include "idle-connection.h"

G_BEGIN_DECLS

void idle_contact_info_finalize (GObject *object);
void idle_contact_info_class_init (IdleConnectionClass *klass);
void idle_contact_info_init (IdleConnection *conn);
void idle_contact_info_iface_init (gpointer g_iface, gpointer iface_data);

G_END_DECLS

#endif /* #ifndef __IDLE_CONTACT_INFO_H__ */
