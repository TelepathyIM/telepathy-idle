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

#define IDLE_DEBUG_FLAG IDLE_DEBUG_PARSER
#include "idle-debug.h"
#include "idle-muc-channel.h"

/* When strict_mode is true, we validate the nick strictly against the IRC
 * RFCs (e.g. only ascii characters, no leading '-'.  When strict_mode is
 * false, we releax the requirements slightly.  This is because we don't want
 * to allow contribute to invalid nicks on IRC, but we want to be able to
 * handle them if another client allows people to use invalid nicks */
gboolean idle_nickname_is_valid(const gchar *nickname, gboolean strict_mode) {
	const gchar *char_pos;

	IDLE_DEBUG("Validating nickname '%s' with strict mode %d", nickname, strict_mode);

	/* FIXME: also check for max length? */
	if (!nickname || *nickname == '\0')
		return FALSE;

	for (char_pos = nickname; *char_pos; char_pos = g_utf8_find_next_char(char_pos, NULL)) {
		/* only used for non-strict checks */
		gunichar ucs4char = g_utf8_get_char_validated(char_pos, -1);

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
				break;

			/* '-' is technically not allowed as first char in a nickname */
			case '-':
				if (strict_mode) {
						if (char_pos == nickname)
							return FALSE;
				}
				break;

			default:
				if (strict_mode) {
						/* only accept ascii characters in strict mode */
						if (!(g_ascii_isalpha(*char_pos) || ((char_pos != nickname) && g_ascii_isdigit(*char_pos)))) {
								IDLE_DEBUG("invalid character '%c'", *char_pos);
								return FALSE;
						}
				} else {
						/* allow unicode and digits as first char in non-strict mode */
						if (!(g_unichar_isalpha(ucs4char) || g_unichar_isdigit(ucs4char))) {
								IDLE_DEBUG("invalid character %d", ucs4char);
								return FALSE;
						}
				}
				break;
		}
	}

	return TRUE;
}

static gboolean _channelname_is_valid(const gchar *channel) {
	static const gchar not_allowed_chars[] = {' ', '\007', ',', '\r', '\n', ':', '\0'};
	gsize len;
	const gchar *tmp;

	if (!idle_muc_channel_is_typechar(channel[0]))
		return FALSE;

	len = strlen(channel);
	if ((len < 2) || (len > 50))
		return FALSE;

	if (channel[0] == '!') {
		for (gsize i = 0; i < 5 && i + 1 < len; i++) {
			if (!g_ascii_isupper(channel[i + 1]) && !isdigit(channel[i + 1]))
				return FALSE;
		}
	}

	tmp = strchr(channel + 1, ':');
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

gchar *idle_normalize_nickname (const gchar *id, GError **error) {
	gchar *normalized;

	if (!idle_nickname_is_valid(id, FALSE)) {
		g_set_error(error, TP_ERROR, TP_ERROR_INVALID_HANDLE, "invalid nickname");
		return NULL;
	}

	normalized = g_utf8_strdown(id, -1);

	return normalized;
}

static gchar *_nick_normalize_func(TpHandleRepoIface *repo, const gchar *id, gpointer ctx, GError **error) {
	return idle_normalize_nickname (id, error);
}

static gchar *_channel_normalize_func(TpHandleRepoIface *repo, const gchar *id, gpointer ctx, GError **error) {
	gchar *normalized;

	if (!_channelname_is_valid(id)) {
		g_set_error(error, TP_ERROR, TP_ERROR_INVALID_HANDLE, "invalid channel ID");
		return NULL;
	}

	normalized = g_utf8_strdown(id, -1);

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

