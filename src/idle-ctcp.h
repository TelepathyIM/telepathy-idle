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

#ifndef __IDLE_CTPC_H__
#define __IDLE_CTCP_H__

#include <glib.h>

#include "idle-connection.h"

G_BEGIN_DECLS

/* Escape, frame and send a CTCP message
 *
 * The return value is a pointer to the first char in the original string which was not sent due to the message getting truncated, or NULL if the whole message was sent.
 */
const gchar *idle_ctcp_privmsg(const gchar *target, const gchar *ctcp, IdleConnection *conn);
const gchar *idle_ctcp_notice(const gchar *target, const gchar *ctcp, IdleConnection *conn);

/* De-escape, deframe and tokenize a CTCP message
 *
 * The return value will be a dynamically allocated array of pointers to dynamically allocated strings which represent the tokens.
 *
 * A NULL return occurs if the given message is not a valid CTCP message.
 *
 * Free with g_strfreev(). */
gchar **idle_ctcp_decode(const gchar *msg);

G_END_DECLS

#endif

