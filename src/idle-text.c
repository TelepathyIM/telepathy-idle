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

#include <time.h>
#include <string.h>

#include <telepathy-glib/text-mixin.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_TEXT
#include "idle-ctcp.h"
#include "idle-debug.h"

gboolean idle_text_decode(const gchar *text, TpChannelTextMessageType *type, gchar **body) {
	gchar *tmp = NULL;

	if (text[0] != '\001') {
		*type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
		tmp = g_strdup(text);
	} else {
		size_t actionlen = strlen("\001ACTION ");
		if (!g_ascii_strncasecmp(text, "\001ACTION ", actionlen)) {
			*type = TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION;
			tmp = g_strndup(text + actionlen, strlen(text + actionlen) - 1);
		} else {
			*body = NULL;
			return FALSE;
		}
	}

	*body = idle_ctcp_kill_blingbling(tmp);
	g_free(tmp);
	return TRUE;
}

GStrv idle_text_encode_and_split(TpChannelTextMessageType type, const gchar *recipient, const gchar *text, GError **error) {
	GPtrArray *messages;
	const gchar *remaining_text = text;
	const gchar * const text_end =  text + strlen(text);
	gchar *header;
	const gchar *footer = "";
	gsize max_bytes;

	switch (type) {
		case TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL:
			header = g_strdup_printf("PRIVMSG %s :", recipient);
			break;
		case TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION:
			header = g_strdup_printf("PRIVMSG %s :\001ACTION ", recipient);
			footer = "\001";
			break;
		case TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE:
			header = g_strdup_printf("NOTICE %s :", recipient);
			break;
		default:
			IDLE_DEBUG("invalid message type %u", type);
			g_set_error(error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "invalid message type %u", type);
			return NULL;
	}

	messages = g_ptr_array_new();
	max_bytes = IRC_MSG_MAXLEN - (strlen(header) + strlen(footer));

	while (remaining_text < text_end) {
		char *newline = strchr(remaining_text, '\n');
		const char *end_iter;
		char *message;

		if (newline != NULL && ((unsigned) (newline - remaining_text)) < max_bytes) {
			/* String up to the next newline is short enough. */
			end_iter = newline;

		} else if ((text_end - remaining_text) > (gint) max_bytes) {
			/* Remaining string is too long; take as many bytes as possible */
			end_iter = remaining_text + max_bytes;
			/* make sure we don't break a UTF-8 code point in half */
			end_iter = g_utf8_find_prev_char (remaining_text, end_iter);
		} else {
			/* Just take it all. */
			end_iter = text_end;
		}

		message = g_strdup_printf("%s%.*s%s", header, (int)(end_iter - remaining_text), remaining_text, footer);
		g_ptr_array_add(messages, message);

		remaining_text = end_iter;
		if (*end_iter == '\n') {
				/* advance over a newline */
				remaining_text++;
		}
	}

	g_assert (remaining_text == text_end);

	g_ptr_array_add(messages, NULL);

	g_free(header);
	return (GStrv) g_ptr_array_free(messages, FALSE);
}

void idle_text_send(GObject *obj, guint type, const gchar *recipient, const gchar *text, IdleConnection *conn, DBusGMethodInvocation *context) {
	time_t timestamp;
	GError *error = NULL;
	GStrv messages;

	if ((recipient == NULL) || (strlen(recipient) == 0)) {
		IDLE_DEBUG("invalid recipient");

		error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "invalid recipient");
		dbus_g_method_return_error(context, error);
		g_error_free(error);

		return;
	}

	messages = idle_text_encode_and_split(type, recipient, text, &error);
	if (messages == NULL) {
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return;
	}

	for(GStrv m = messages; *m != NULL; m++) {
		idle_connection_send(conn, *m);
	}

	g_strfreev(messages);

	timestamp = time(NULL);
	tp_svc_channel_type_text_emit_sent(obj, timestamp, type, text);

	tp_svc_channel_type_text_return_from_send(context);
}

