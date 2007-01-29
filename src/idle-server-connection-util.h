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

#ifndef __IDLE_SERVER_CONNECTION_UTIL_H__
#define __IDLE_SERVER_CONNECTION_UTIL_H__

typedef void (*message_cb)(const gchar *msg, gpointer user_data);

void msg_split(gchar *msg, gchar *splitbuf, gsize max_len, message_cb cb, gpointer user_data);

typedef struct _IdleOutputPendingMsg IdleOutputPendingMsg;

struct _IdleOutputPendingMsg
{
	gchar *message;
	guint priority;
};

#define idle_output_pending_msg_new() \
	(g_slice_new(IdleOutputPendingMsg))
#define idle_output_pending_msg_new0() \
	(g_slice_new0(IdleOutputPendingMsg))

void idle_output_pending_msg_free(IdleOutputPendingMsg *msg);

#define SERVER_CMD_MIN_PRIORITY 0
#define SERVER_CMD_NORMAL_PRIORITY G_MAXUINT/2
#define SERVER_CMD_MAX_PRIORITY G_MAXUINT

gint pending_msg_compare(gconstpointer a, gconstpointer b, gpointer unused);

#define IRC_MSG_MAXLEN 510

/* From RFC 2813 :
 * This in essence means that the client may send one (1) message every
 * two (2) seconds without being adversely affected.  Services MAY also
 * be subject to this mechanism.
 */

#define MSG_QUEUE_UNLOAD_AT_A_TIME 1
#define MSG_QUEUE_TIMEOUT 2

#define CONNECTION_TIMEOUT 15000

#endif
