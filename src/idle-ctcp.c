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

#include "idle-ctcp.h"

/* FIXME add escaping */
static void _ctcp_send(const gchar *send_cmd, const gchar *target, const gchar *ctcp, IdleConnection *conn) {
	gchar buf[IRC_MSG_MAXLEN + 1] = {0};
	gsize len = snprintf(buf, IRC_MSG_MAXLEN, "%s %s :\001%s", send_cmd, target, ctcp);

	buf[len] = '\001';
	buf[len + 1] = '\000';

	_idle_connection_send(conn, buf);
}

void idle_ctcp_privmsg(const gchar *target, const gchar *ctcp, IdleConnection *conn) {
	_ctcp_send("PRIVMSG", target, ctcp, conn);
}

void idle_ctcp_notice(const gchar *target, const gchar *ctcp, IdleConnection *conn) {
	_ctcp_send("NOTICE", target, ctcp, conn);
}

