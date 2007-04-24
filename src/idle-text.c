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

#include "idle-text.h"

#include <telepathy-glib/text-mixin.h>

#include <time.h>
#include <string.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_TEXT
#include "idle-debug.h"

void idle_text_decode(const gchar *text, TpChannelTextMessageType *type, gchar **body) {
	if (text[0] != '\001') {
		*type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
		*body = g_strdup(text);
	} else {
		size_t actionlen = strlen("\001ACTION ");
		if (!g_ascii_strncasecmp(text, "\001ACTION ", actionlen)) {
			*type = TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION;
			*body = g_strndup(text + actionlen, strlen(text + actionlen) - 1);
		} else {
			*type = -1;
			*body = NULL;
		}
	}
}

void idle_text_send (GObject *obj, guint type, const gchar *recipient, const gchar *text, IdleConnection *conn, DBusGMethodInvocation *context)
{
	gchar msg[IRC_MSG_MAXLEN+1];
	time_t timestamp;
	const gchar *final_text = text;
	gsize len;
	gchar *part;
	gsize headerlen;
	GError *error;

	if ((recipient == NULL) || (strlen(recipient) == 0))
	{
		IDLE_DEBUG("invalid recipient");

		error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "invalid recipient");
		dbus_g_method_return_error(context, error);
		g_error_free(error);

		return;
	}

	len = strlen(final_text);
	part = (gchar*)final_text;

	if (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL)
	{
		g_snprintf(msg, IRC_MSG_MAXLEN+1, "PRIVMSG %s :", recipient);
	}
	else if (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION)
	{
		g_snprintf(msg, IRC_MSG_MAXLEN+1, "PRIVMSG %s :\001ACTION ", recipient);
	}
	else if (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE)
	{
		g_snprintf(msg, IRC_MSG_MAXLEN+1, "NOTICE %s :", recipient);
	}
	else
	{
		IDLE_DEBUG("invalid message type %u", type);

		error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "invalid message type %u", type);
		dbus_g_method_return_error(context, error);
		g_error_free(error);

		return;
	}

	headerlen = strlen(msg);

	while (part < final_text+len)
	{
		char *br = strchr (part, '\n');
		size_t len = IRC_MSG_MAXLEN-headerlen;
		if (br)
		{
			len = (len < br - part) ? len : br - part;
		}

		if (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION)
		{
			g_snprintf(msg+headerlen, len + 1, "%s\001", part);
			len -= 1;
		}
		else
		{
			g_strlcpy(msg+headerlen, part, len + 1);
		}
		part += len;
		if (br)
		{
			part++;
		}

		_idle_connection_send(conn, msg);
	}

	timestamp = time(NULL);
  tp_svc_channel_type_text_emit_sent(obj, timestamp, type, text);

	tp_svc_channel_type_text_return_from_send(context);
}

