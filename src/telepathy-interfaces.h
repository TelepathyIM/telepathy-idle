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

#ifndef __TELEPATHY_INTERFACES_H__
#define __TELEPATHY_INTERFACES_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define TP_IFACE_CHANNEL_INTERFACE \
        "org.freedesktop.Telepathy.Channel"
#define TP_IFACE_CHANNEL_INTERFACE_GROUP \
        "org.freedesktop.Telepathy.Channel.Interface.Group"
#define TP_IFACE_CHANNEL_INTERFACE_HOLD \
        "org.freedesktop.Telepathy.Channel.Interface.Hold"
#define TP_IFACE_CHANNEL_INTERFACE_PASSWORD \
        "org.freedesktop.Telepathy.Channel.Interface.Password"
#define TP_IFACE_CHANNEL_INTERFACE_TRANSFER \
        "org.freedesktop.Telepathy.Channel.Interface.Transfer"
#define TP_IFACE_CHANNEL_TYPE_ROOM_LIST \
        "org.freedesktop.Telepathy.Channel.Type.RoomList"
#define TP_IFACE_CHANNEL_TYPE_TEXT \
        "org.freedesktop.Telepathy.Channel.Type.Text"
#define TP_IFACE_CONN_INTERFACE \
        "org.freedesktop.Telepathy.Connection"
#define TP_IFACE_CONN_INTERFACE_ALIASING \
        "org.freedesktop.Telepathy.Connection.Interface.Aliasing"
#define TP_IFACE_CONN_INTERFACE_CAPABILITIES \
        "org.freedesktop.Telepathy.Connection.Interface.Capabilities"
#define TP_IFACE_CONN_INTERFACE_CONTACT_INFO \
        "org.freedesktop.Telepathy.Connection.Interface.ContactInfo"
#define TP_IFACE_CONN_INTERFACE_FORWARDING \
        "org.freedesktop.Telepathy.Connection.Interface.Forwarding"
#define TP_IFACE_CONN_INTERFACE_PRESENCE \
        "org.freedesktop.Telepathy.Connection.Interface.Presence"
#define TP_IFACE_CONN_INTERFACE_PRIVACY \
        "org.freedesktop.Telepathy.Connection.Interface.Privacy"
#define TP_IFACE_CONN_INTERFACE_RENAMING \
        "org.freedesktop.Telepathy.Connection.Interface.Renaming"
#define TP_IFACE_CONN_MGR_INTERFACE \
        "org.freedesktop.Telepathy.ConnectionManager"
#define TP_IFACE_PROPERTIES \
		"org.freedesktop.Telepathy.Properties"

G_END_DECLS

#endif /* #ifndef __TELEPATHY_INTERFACES_H__*/
