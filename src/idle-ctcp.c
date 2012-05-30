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

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const gchar *_ctcp_send(const gchar *send_cmd, const gchar *target, const gchar *ctcp, IdleConnection *conn) {
	gchar buf[IRC_MSG_MAXLEN + 1] = {'\0'};
	int out_index = snprintf(buf, IRC_MSG_MAXLEN, "%s %s :\001", send_cmd, target);

	const gchar *iter;
	for (iter = ctcp; *iter != '\0'; iter++) {
		switch (*iter) {
			case '\r':
			case '\n':
			case '\001':
				if ((out_index + 4) < IRC_MSG_MAXLEN) {
					snprintf(buf + out_index, 5, "\\%03o", (unsigned int) *iter);
					out_index += 4;
				} else {
					goto out;
				}
				break;

			case '\\':
				if ((out_index + 2) < IRC_MSG_MAXLEN) {
					buf[out_index++] = '\\';
					buf[out_index++] = '\\';
				} else {
					 goto out;
				}
				break;

			default:
				buf[out_index++] = *iter;
		}

		if (out_index >= (IRC_MSG_MAXLEN - 1)) {
			iter++;
			break;
		}
	}

out:

	buf[out_index++] = '\001';
	buf[out_index++] = '\000';

	idle_connection_send(conn, buf);

	if (*iter == '\0')
		return NULL;
	else
		return iter;
}

const gchar *idle_ctcp_privmsg(const gchar *target, const gchar *ctcp, IdleConnection *conn) {
	return _ctcp_send("PRIVMSG", target, ctcp, conn);
}

const gchar *idle_ctcp_notice(const gchar *target, const gchar *ctcp, IdleConnection *conn) {
	return _ctcp_send("NOTICE", target, ctcp, conn);
}

gchar *idle_ctcp_kill_blingbling(const gchar *msg) {
	gchar *killed, *killed_iter;

	if (msg == NULL)
		return NULL;

	killed = g_malloc0(strlen(msg) + 1);
	killed_iter = killed;

	for (const gchar *iter = msg; *iter != '\0';) {
		switch (*iter) {
			case '\x03': /* ^C */
				iter++;

				while (isdigit(*iter))
					iter++;

				if (*iter == ',') {
					iter++;

					while (isdigit(*iter))
						iter++;
				}
				break;

			case '\x02':
			case '\x0f':
			case '\x11':
			case '\x12':
			case '\x16':
			case '\x1d':
			case '\x1f':
				iter++;
				break;

			default:
				*killed_iter++ = *iter++;
		}
	}

	return killed;
}

gchar **idle_ctcp_decode(const gchar *msg) {
	GPtrArray *tokens;
	gchar cur_token[IRC_MSG_MAXLEN] = {'\0'};
	gchar *cur_iter = cur_token;
	gchar **ret;
	const gchar *iter;
	gboolean string = FALSE;

	if (!msg || (msg[0] != '\001') || !msg[1] || (msg[1] == '\001'))
		return NULL;

	tokens = g_ptr_array_new();

	iter = msg + 1;
	while (*iter != '\0') {
		switch (*iter) {
			case '\\':
				if (isdigit(iter[1])) {
					gchar *endptr = NULL;
					gchar escaped_char = (gchar) strtol(iter + 1, &endptr, 8);

					if (endptr != (iter + 1)) {
						*cur_iter++ = escaped_char;
						iter = endptr;
					} else {
						iter++;
					}
				} else {
					*cur_iter++ = iter[1];
					iter += 2;
				}
				break;

			case ' ':
				if (string) {
					*cur_iter++ = ' ';
				} else {
					if (cur_token[0] != '\0') {
						g_ptr_array_add(tokens, g_strdup(cur_token));
						memset(cur_token, '\0', IRC_MSG_MAXLEN);
						cur_iter = cur_token;
					}
				}

				iter++;
				break;

			case '"':
				if (cur_token[0] != '\0') {
					g_ptr_array_add(tokens, g_strdup(cur_token));
					memset(cur_token, 0, IRC_MSG_MAXLEN);
					cur_iter = cur_token;
				}

				string ^= TRUE;

				iter++;
				break;

			case '\001':
				iter++;
				break;

			default:
				*cur_iter++ = *iter++;
				break;
		}
	}

	if (cur_token[0] != '\0')
		g_ptr_array_add(tokens, g_strdup(cur_token));

	g_ptr_array_add(tokens, NULL);

	ret = (gchar **) tokens->pdata;
	g_ptr_array_free(tokens, FALSE);

	return ret;
}

