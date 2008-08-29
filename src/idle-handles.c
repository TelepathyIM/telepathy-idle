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

#include "idle-handles.h"

#include <glib.h>
#include <ctype.h>
#include <string.h>

#include <telepathy-glib/errors.h>
#include <telepathy-glib/handle-repo-dynamic.h>

static gboolean _nickname_is_valid(const gchar *nickname) {
	gsize len;
	gunichar ucs4char;
	const gchar *char_pos = nickname;

	len = g_utf8_strlen(nickname, -1);

	if (!len)
		return FALSE;

	while (char_pos != NULL) {
		ucs4char = g_utf8_get_char_validated(char_pos, -1);

		switch (*char_pos) {
			case '[':
			case ']':
			case '\\':
			case '`':
			case '_':
			case '^':
			case '{':
			case '|':
			case '}':
			case '-':
				break;

			case '\0':
				return TRUE;
				break;

			default:
				if (!(g_unichar_isalpha(ucs4char) || ((char_pos != nickname) && g_unichar_isdigit(ucs4char))))
					return FALSE;
				break;
		}

		char_pos = g_utf8_find_next_char(char_pos, NULL);
	}

	return TRUE;
}

static gboolean _channelname_is_valid(const gchar *channel) {
	static const gchar not_allowed_chars[] = {' ', '\007', ',', '\r', '\n', ':', '\0'};

	if ((channel[0] != '#') && (channel[0] != '!') && (channel[0] != '&') && (channel[0] != '+'))
		return FALSE;

	gsize len = strlen(channel);
	if ((len < 2) || (len > 50))
		return FALSE;

	if (channel[0] == '!') {
		for (int i = 0; i < 5; i++) {
			if (!g_ascii_isupper(channel[i + 1]) && !isdigit(channel[i + 1]))
				return FALSE;
		}
	}

	const gchar *tmp = strchr(channel + 1, ':');
	if (tmp != NULL) {
		for (const gchar *tmp2 = channel + 1; tmp2 != tmp; tmp2++) {
			if (strchr(not_allowed_chars, *tmp2))
				return FALSE;
		}

		for (const gchar *tmp2 = tmp + 1; tmp2 != channel + len; tmp2++) {
			if (strchr(not_allowed_chars, *tmp2))
				return FALSE;
		}
	} else {
		for (const gchar *tmp2 = channel + 1; tmp2 != channel + len; tmp2++) {
			if (strchr(not_allowed_chars, *tmp2))
				return FALSE;
		}
	}

	return TRUE;
}

static gchar *_nick_normalize_func(TpHandleRepoIface *repo, const gchar *id, gpointer ctx, GError **error) {
	if (!_nickname_is_valid(id)) {
		g_set_error(error, TP_ERRORS, TP_ERROR_INVALID_HANDLE, "invalid nickname");
		return NULL;
	}

	gchar *normalized = g_utf8_strdown(id, -1);

	return normalized;
}

static gchar *_channel_normalize_func(TpHandleRepoIface *repo, const gchar *id, gpointer ctx, GError **error) {
	gchar *channel = g_strdup(id);

	if ((channel[0] != '#') && (channel[0] != '!') && (channel[0] != '&') && (channel[0] != '+')) {
		gchar *tmp = channel;
		channel = g_strdup_printf("#%s", channel);
		g_free(tmp);
	}

	if (!_channelname_is_valid(channel)) {
		g_set_error(error, TP_ERRORS, TP_ERROR_INVALID_HANDLE, "invalid channel ID");
		return NULL;
	}

	gchar *normalized = g_utf8_strdown(channel, -1);

	g_free(channel);

	return normalized;
}

void idle_handle_repos_init(TpHandleRepoIface **handles) {
	g_assert(handles != NULL);

	handles[TP_HANDLE_TYPE_CONTACT] = (TpHandleRepoIface *) g_object_new(TP_TYPE_DYNAMIC_HANDLE_REPO,
			"handle-type", TP_HANDLE_TYPE_CONTACT,
			"normalize-function", _nick_normalize_func,
			"default-normalize-context", NULL,
			NULL);

	handles[TP_HANDLE_TYPE_ROOM] = (TpHandleRepoIface *) g_object_new(TP_TYPE_DYNAMIC_HANDLE_REPO,
			"handle-type", TP_HANDLE_TYPE_ROOM,
			"normalize-function", _channel_normalize_func,
			"default-normalize-context", NULL,
			NULL);
}

