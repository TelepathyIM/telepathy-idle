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

#include <glib.h>
#include <string.h>
#include <ctype.h>
#include <telepathy-glib/heap.h>

#include "idle-handles.h"
#include "idle-handles-private.h"

#include "idle-connection.h"

#define idle_handle_priv_new() (g_slice_new0(IdleHandlePriv))

gboolean idle_nickname_is_valid(const gchar *nickname)
{
	gsize len;
	gunichar ucs4char;
	const gchar *char_pos = nickname;

	len = g_utf8_strlen(nickname, -1);

	if (!len)
	{
		return FALSE;
	}

	while (char_pos != NULL)
	{
		ucs4char = g_utf8_get_char_validated(char_pos, -1);

		switch (*char_pos)
		{
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
			{
				return TRUE;
			}
			break;
			default:
			{
				if (!(g_unichar_isalpha(ucs4char) || ((char_pos != nickname) && g_unichar_isdigit(ucs4char))))
				{
					return FALSE;
				}
			}
			break;
		}
		
		char_pos = g_utf8_find_next_char(char_pos, NULL);
	}

	return TRUE;
}

gboolean idle_channelname_is_valid(const gchar *channel)
{
	const static gchar not_allowed_chars[6] = {' ', '\007', ',', '\r', '\n', ':'};
	const gchar *tmp, *tmp2;
	int i;
	gsize len;
	
	if ((channel[0] != '#') && (channel[0] != '!') && (channel[0] != '&') && (channel[0] != '+'))
	{
		return FALSE;
	}

	len = strlen(channel);
	
	if ((len < 2) || (len > 50))
	{
		return FALSE;
	}

	if (channel[0] == '!')
	{
		for (i=0; i<5; i++)
		{		
			if (!g_ascii_isupper(channel[i+1]) && !isdigit(channel[i+1]))
			{
				return FALSE;
			}
		}
	}

	tmp = strchr(channel+1, ':');

	if (tmp != NULL)
	{
		for (tmp2 = channel+1; tmp2 != tmp; tmp2++)
		{
			for (i=0; i<6; i++)
			{
				if (*tmp2 == not_allowed_chars[i])
				{
					return FALSE;
				}
			}
		}
		
		for (tmp2 = tmp+1; tmp2 != channel+len; tmp2++)
		{
			for (i=0; i<6; i++)
			{
				if (*tmp2 == not_allowed_chars[i])
				{
					return FALSE;
				}
			}
		}
	}
	else
	{	
		for (tmp2 = channel+1; tmp2 != channel+len; tmp2++)
		{
			for (i=0; i<6; i++)
			{
				if (*tmp2 == not_allowed_chars[i])
				{
					return FALSE;
				}
			}
		}
		
	}

	return TRUE;
}

static void handle_priv_free(IdleHandlePriv *priv)
{
	g_assert(priv != NULL);
	g_free(priv->string);

	if (priv->cp != NULL)
	{
		idle_contact_presence_free(priv->cp);
	}
	
	g_slice_free(IdleHandlePriv, priv);
}

static IdleHandle idle_handle_alloc(IdleHandleStorage *storage, TpHandleType type)
{
	IdleHandle ret;
	
	g_assert(storage != NULL);

	switch (type)
	{
		case TP_HANDLE_TYPE_CONTACT:
		{
			ret = GPOINTER_TO_INT(tp_heap_extract_first(storage->contact_unused));

			if (ret == 0)
			{
				ret = storage->contact_serial++;
			}
		}
		break;
		case TP_HANDLE_TYPE_ROOM:
		{
			ret = GPOINTER_TO_INT(tp_heap_extract_first(storage->room_unused));

			if (ret == 0)
			{
				ret = storage->room_serial++;
			}
		}
		break;
		case TP_HANDLE_TYPE_LIST:
		default:
		{
			g_debug("%s: unsupported handle type %u", G_STRFUNC, type);
			ret = 0;
		}
		break;
	}

/*	g_debug("%s: returning %u (type %u)", G_STRFUNC, ret, type);*/

	return ret;
}

IdleHandlePriv *handle_priv_lookup(IdleHandleStorage *storage, TpHandleType type, IdleHandle handle)
{
	IdleHandlePriv *priv;

	g_assert(storage != NULL);

	if (handle == 0)
	{
		return NULL;
	}

	switch (type)
	{
		case TP_HANDLE_TYPE_CONTACT:
		{
			priv = g_hash_table_lookup(storage->contact_handles, GINT_TO_POINTER(handle));
		}
		break;
		case TP_HANDLE_TYPE_ROOM:
		{
			priv = g_hash_table_lookup(storage->room_handles, GINT_TO_POINTER(handle));
		}
		break;
		case TP_HANDLE_TYPE_LIST:
		default:
		{
			g_critical("%s: Only TP_HANDLE_TYPE_CONTACT and TP_HANDLE_TYPE_ROOM supported!", G_STRFUNC);
			return NULL;
		}
		break;
	}

	return priv;
}

void handle_priv_remove(IdleHandleStorage *storage, TpHandleType type, IdleHandlePriv *priv, IdleHandle handle)
{
	g_assert(storage != NULL);
	
	switch (type)
	{
		case TP_HANDLE_TYPE_CONTACT:
		{
			g_hash_table_remove(storage->contact_strings, priv->string);
			g_hash_table_remove(storage->contact_handles, GINT_TO_POINTER(handle));
			
			if (handle == storage->contact_serial-1)
			{
				/* take advantage of good luck ;) */
				storage->contact_serial--;
			}
			else
			{
				tp_heap_add(storage->contact_unused, GINT_TO_POINTER(handle));
			}			
		}
		break;
		case TP_HANDLE_TYPE_ROOM:
		{
			g_hash_table_remove(storage->room_strings, priv->string);
			g_hash_table_remove(storage->room_handles, GINT_TO_POINTER(handle));
			
			if (handle == storage->room_serial-1)
			{
				storage->room_serial--;
			}
			else
			{
				tp_heap_add(storage->room_unused, GINT_TO_POINTER(handle));
			}			
		}
		break;
		case TP_HANDLE_TYPE_LIST:
		default:
		{
			g_critical("%s: Only TP_HANDLE_TYPE_CONTACT and TP_HANDLE_TYPE_ROOM supported!", G_STRFUNC);
			return;
		}
		break;
	}
}

gboolean idle_handle_type_is_valid(TpHandleType type)
{
	switch (type)
	{
		case TP_HANDLE_TYPE_CONTACT:
		case TP_HANDLE_TYPE_ROOM:
		{
			return TRUE;
		}
		break;
		case TP_HANDLE_TYPE_LIST:
		default:
		{
			return FALSE;
		}
		break;
	}
}

static guint g_strncase_hash(gconstpointer key)
{
	guint ret;
	gchar *tmp;

	tmp = g_utf8_strdown(key, 32);
	ret = g_str_hash(tmp);

	g_free(tmp);

	return ret;
}

static gboolean g_strncase_equal(gconstpointer a, gconstpointer b)
{
	gchar *s1, *s2;
	gboolean ret;

	s1 = g_utf8_casefold(a, -1);
	s2 = g_utf8_casefold(b, -1);

	ret = (strcmp(s1, s2) == 0);

	g_free(s1);
	g_free(s2);
	
	return ret;
}

static gint idle_handle_compare(gconstpointer a, gconstpointer b)
{
	return (a < b) ? -1 : (a == b) ? 0 : 1;
}

IdleHandleStorage *idle_handle_storage_new()
{
	IdleHandleStorage *ret;

	ret = g_slice_new0(IdleHandleStorage);

	ret->contact_handles = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)(handle_priv_free));
	ret->room_handles = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)(handle_priv_free));

	ret->contact_strings = g_hash_table_new_full(g_strncase_hash, g_strncase_equal, NULL, NULL);
	ret->room_strings = g_hash_table_new_full(g_strncase_hash, g_strncase_equal, NULL, NULL);

	ret->contact_unused = tp_heap_new(idle_handle_compare);
	ret->room_unused = tp_heap_new(idle_handle_compare);

	ret->contact_serial = 1;
	ret->room_serial = 1;

	return ret;
}

void idle_handle_storage_destroy(IdleHandleStorage *storage)
{
	g_assert(storage != NULL);
	g_assert(storage->contact_handles != NULL);
	g_assert(storage->room_handles != NULL);

	g_hash_table_destroy(storage->contact_handles);
	g_hash_table_destroy(storage->room_handles);
	
	g_hash_table_destroy(storage->contact_strings);
	g_hash_table_destroy(storage->room_strings);

	tp_heap_destroy(storage->contact_unused);
	tp_heap_destroy(storage->room_unused);

	g_slice_free(IdleHandleStorage, storage);
}

gboolean idle_handle_is_valid(IdleHandleStorage *storage, TpHandleType type, IdleHandle handle)
{
	return (handle_priv_lookup(storage, type, handle) != NULL);
}

gboolean idle_handle_ref(IdleHandleStorage *storage, TpHandleType type, IdleHandle handle)
{
	IdleHandlePriv *priv;

	priv = handle_priv_lookup(storage, type, handle);

	if (priv == NULL)
	{
		return FALSE;
	}
	else
	{
		priv->refcount++;
		return TRUE;
	}
}

gboolean idle_handle_unref(IdleHandleStorage *storage, TpHandleType type, IdleHandle handle)
{
	IdleHandlePriv *priv;

	priv = handle_priv_lookup(storage, type, handle);

	if (priv == NULL)
	{
		return FALSE;
	}
	else
	{
		g_assert(priv->refcount > 0);

		priv->refcount--;

		if (priv->refcount == 0)
		{
			handle_priv_remove(storage, type, priv, handle);
		}

		return TRUE;
	}
}

const char *idle_handle_inspect(IdleHandleStorage *storage, TpHandleType type, IdleHandle handle)
{
	IdleHandlePriv *priv;

	priv = handle_priv_lookup(storage, type, handle);

	if (priv == NULL)
	{
		return NULL;
	}
	else
	{
		return priv->string;
	}
}

IdleHandle idle_handle_for_contact(IdleHandleStorage *storage, const char *nickname)
{
	IdleHandle handle;
	
	g_assert(storage != NULL);

	if ((!nickname) || (nickname[0] == '\0'))
	{
		g_debug("%s: handle for invalid nickname requested", G_STRFUNC);
		return 0;
	}	

	if (!idle_nickname_is_valid(nickname))
	{
		g_debug("%s: nickname (%s) isn't valid!", G_STRFUNC, nickname);
		return 0;
	}
	
	handle = GPOINTER_TO_INT(g_hash_table_lookup(storage->contact_strings, nickname));

	if (handle == 0)
	{
		handle = idle_handle_alloc(storage, TP_HANDLE_TYPE_CONTACT);
	}
	
	if (handle_priv_lookup(storage, TP_HANDLE_TYPE_CONTACT, handle) == NULL)
	{
		IdleHandlePriv *priv;
		priv = idle_handle_priv_new();
		priv->string = g_strdup(nickname);

		g_hash_table_insert(storage->contact_handles, GINT_TO_POINTER(handle), priv);
		g_hash_table_insert(storage->contact_strings, priv->string, GINT_TO_POINTER(handle));
	}

	return handle;
}

gboolean idle_handle_for_room_exists(IdleHandleStorage *storage, const char *channel_up)
{
	IdleHandle handle;
	gchar *channel;

	g_assert(storage != NULL);

	channel = g_ascii_strdown(channel_up, -1);

	if ((channel == NULL) || (channel[0] == '\0'))
	{
		return FALSE;
	}
	else
	{
		handle = GPOINTER_TO_INT(g_hash_table_lookup(storage->room_strings, channel));
	}

	g_free(channel);

	return handle_priv_lookup(storage, TP_HANDLE_TYPE_ROOM, handle) != NULL;
}

IdleHandle idle_handle_for_room(IdleHandleStorage *storage, const char *channel)
{
	IdleHandle handle;

	g_assert(storage != NULL);

	if ((channel == NULL) || (channel[0] == '\0'))
	{
		g_debug("%s: handle for a invalid channel requested", G_STRFUNC);
		return 0;
	}

	if (!idle_channelname_is_valid(channel))
	{
		g_debug("%s: channel name (%s) not valid!", G_STRFUNC, channel);
		return 0;
	}
	
	handle = GPOINTER_TO_INT(g_hash_table_lookup(storage->room_strings, channel));

	if (handle == 0)
	{
		handle = idle_handle_alloc(storage, TP_HANDLE_TYPE_ROOM);
	}

	if (handle_priv_lookup(storage, TP_HANDLE_TYPE_ROOM, handle) == NULL)
	{
		IdleHandlePriv *priv;
		priv = idle_handle_priv_new();
		priv->string = g_strdup(channel);

		g_hash_table_insert(storage->room_handles, GINT_TO_POINTER(handle), priv);
		g_hash_table_insert(storage->room_strings, priv->string, GINT_TO_POINTER(handle));
	}

	return handle;
}

gboolean idle_handle_set_presence(IdleHandleStorage *storage, IdleHandle handle, IdleContactPresence *cp)
{
	IdleHandlePriv *priv;

	g_assert(storage != NULL);
	g_assert(handle != 0);

	priv = handle_priv_lookup(storage, TP_HANDLE_TYPE_CONTACT, handle);

	if (priv == NULL)
	{
		return FALSE;
	}
	else
	{
		priv->cp = cp;
	}

	return TRUE;
}

IdleContactPresence *idle_handle_get_presence(IdleHandleStorage *storage, IdleHandle handle)
{
	IdleHandlePriv *priv;

	g_assert(storage != NULL);
	g_assert(handle != 0);

	priv = handle_priv_lookup(storage, TP_HANDLE_TYPE_CONTACT, handle);

	if (priv == NULL)
	{
		return NULL;
	}
	else
	{
		return priv->cp;
	}
}

