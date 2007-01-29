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

#ifndef __TELEPATHY_CONSTANTS_H__
#define __TELEPATHY_CONSTANTS_H__

#include <glib.h>
G_BEGIN_DECLS

typedef enum {
TP_CONN_MGR_PARAM_FLAG_REQUIRED = 1,
TP_CONN_MGR_PARAM_FLAG_REGISTER = 2,
TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT = 4
} TpConnectionManagerParamFlag;

typedef enum {
TP_HANDLE_TYPE_NONE = 0,
TP_HANDLE_TYPE_CONTACT = 1,
TP_HANDLE_TYPE_ROOM = 2,
TP_HANDLE_TYPE_LIST = 3
} TpHandleType;

typedef enum {
TP_CONN_PRESENCE_TYPE_UNSET = 0,
TP_CONN_PRESENCE_TYPE_OFFLINE = 1,
TP_CONN_PRESENCE_TYPE_AVAILABLE = 2,
TP_CONN_PRESENCE_TYPE_AWAY = 3,
TP_CONN_PRESENCE_TYPE_EXTENDED_AWAY = 4,
TP_CONN_PRESENCE_TYPE_HIDDEN = 5
} TpConnectionPresenceType;

typedef enum {
TP_CONN_STATUS_CONNECTED = 0,
TP_CONN_STATUS_CONNECTING = 1,
TP_CONN_STATUS_DISCONNECTED = 2
} TpConnectionStatus;

typedef enum {
TP_CONN_STATUS_REASON_NONE_SPECIFIED = 0,
TP_CONN_STATUS_REASON_REQUESTED = 1,
TP_CONN_STATUS_REASON_NETWORK_ERROR = 2,
TP_CONN_STATUS_REASON_AUTHENTICATION_FAILED = 3,
TP_CONN_STATUS_REASON_ENCRYPTION_ERROR = 4,
TP_CONN_STATUS_REASON_NAME_IN_USE = 5
} TpConnectionStatusReason;

typedef enum {
TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL = 0,
TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION = 1,
TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE = 2
} TpChannelTextMessageType;

typedef enum {
TP_CHANNEL_GROUP_FLAG_CAN_ADD = 1,
TP_CHANNEL_GROUP_FLAG_CAN_REMOVE = 2,
TP_CHANNEL_GROUP_FLAG_CAN_RESCIND = 4,
TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD = 8,
TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE = 16,
TP_CHANNEL_GROUP_FLAG_MESSAGE_ACCEPT = 32,
TP_CHANNEL_GROUP_FLAG_MESSAGE_REJECT = 64,
TP_CHANNEL_GROUP_FLAG_MESSAGE_RESCIND = 128
} TpChannelGroupFlags;

typedef enum {
TP_CHANNEL_HOLD_STATE_NONE = 0,
TP_CHANNEL_HOLD_STATE_SEND_ONLY = 1,
TP_CHANNEL_HOLD_STATE_RECV_ONLY = 2,
TP_CHANNEL_HOLD_STATE_BOTH = 3
} TpChannelHoldState;

typedef enum {
TP_CHANNEL_PASSWORD_FLAG_PROVIDE = 8
} TpChannelPasswordFlags;

typedef enum {
	TP_PROPERTY_FLAG_READ = 1,
	TP_PROPERTY_FLAG_WRITE = 2
} TpPropertyFlags;
	
typedef enum {
	TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN = 0,
	TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE = 1,
	TP_CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT = 2,
	TP_CHANNEL_TEXT_SEND_ERROR_PERMISSION_DENIED = 3,
	TP_CHANNEL_TEXT_SEND_ERROR_TOO_LONG = 4
} TpChannelTextSenderror;

typedef enum {
	TP_CHANNEL_GROUP_CHANGE_REASON_NONE = 0,
	TP_CHANNEL_GROUP_CHANGE_REASON_OFFLINE = 1,
	TP_CHANNEL_GROUP_CHANGE_REASON_KICKED = 2,
	TP_CHANNEL_GROUP_CHANGE_REASON_BUSY = 3,
	TP_CHANNEL_GROUP_CHANGE_REASON_INVITED = 4
}
TpChannelGroupChangeReason;

G_END_DECLS


#endif
