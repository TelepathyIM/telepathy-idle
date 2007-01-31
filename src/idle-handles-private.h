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

#ifndef __IDLE_HANDLES_PRIVATE_H__
#define __IDLE_HANDLES_PRIVATE_H__

#include <glib.h>
#include <telepathy-glib/heap.h>

#include "idle-connection.h"

typedef struct _IdleHandlePriv IdleHandlePriv;

struct _IdleHandlePriv
{
	guint refcount;
	gchar *string;
	IdleContactPresence *cp;
};

struct _IdleHandleStorage
{	
	GHashTable *contact_handles;
	GHashTable *room_handles;

	GHashTable *contact_strings;
	GHashTable *room_strings;

	guint contact_serial;
	guint room_serial;

	TpHeap *contact_unused;
	TpHeap *room_unused;
};

#endif 
