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

#ifndef __IDLE_CONNECTION_H__
#define __IDLE_CONNECTION_H__

#include <glib-object.h>
#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/handle.h>
#include <telepathy-glib/contacts-mixin.h>

#include "idle-parser.h"

#define IRC_MSG_MAXLEN 510

G_BEGIN_DECLS

typedef struct _IdleConnection IdleConnection;
typedef struct _IdleConnectionClass IdleConnectionClass;
typedef struct _IdleConnectionPrivate IdleConnectionPrivate;

struct _IdleConnectionClass {
	TpBaseConnectionClass parent_class;
	TpContactsMixinClass contacts;
};

struct _IdleConnection {
	TpBaseConnection parent;
	TpContactsMixin contacts;
	IdleParser *parser;
	GQueue *contact_info_requests;
	IdleConnectionPrivate *priv;
};

GType idle_connection_get_type(void);

/* TYPE MACROS */
#define IDLE_TYPE_CONNECTION \
	(idle_connection_get_type())
#define IDLE_CONNECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), IDLE_TYPE_CONNECTION, IdleConnection))
#define IDLE_CONNECTION_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), IDLE_TYPE_CONNECTION, IdleConnectionClass))
#define IDLE_IS_CONNECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_CONNECTION))
#define IDLE_IS_CONNECTION_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_CONNECTION))
#define IDLE_CONNECTION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS((obj), IDLE_TYPE_CONNECTION, IdleConnectionClass))

void idle_connection_canon_nick_receive(IdleConnection *conn, TpHandle handle, const gchar *canon_nick);
void idle_connection_emit_queued_aliases_changed(IdleConnection *conn);
void idle_connection_send(IdleConnection *conn, const gchar *msg);
gsize idle_connection_get_max_message_length(IdleConnection *conn);
const gchar * const *idle_connection_get_implemented_interfaces (void);

G_END_DECLS

#endif /* #ifndef __IDLE_CONNECTION_H__*/
