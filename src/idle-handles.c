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

#include "idle-connection.h"

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

static GQuark
idle_handle_real_quark()
{
  static GQuark quark = 0;

  if (!quark)
  {
    quark = g_quark_from_static_string("idle_handle_real");
  }
  
  return quark;
}

static GQuark
idle_handle_presence_quark()
{
  static GQuark quark = 0;

  if (!quark)
  {
    quark = g_quark_from_static_string("idle_handle_presence");
  }
  
  return quark;
}

const gchar *
idle_handle_inspect(TpHandleRepoIface *storage, TpHandle handle)
{
  g_assert(storage != NULL);
  g_assert(tp_handle_is_valid(storage, handle, NULL));

  return tp_handle_get_qdata(storage, handle, idle_handle_real_quark());
}

TpHandle idle_handle_for_contact(TpHandleRepoIface *storage, const char *nickname)
{
	TpHandle handle;
  gchar *nickname_down;
	
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

  nickname_down = g_utf8_strdown(nickname, -1);

  handle = tp_handle_request(storage, nickname_down, FALSE);

  if (!handle)
  {
    handle = tp_handle_request(storage, nickname_down, TRUE);
    g_assert(tp_handle_set_qdata(storage, handle, idle_handle_real_quark(), g_strdup(nickname), (GDestroyNotify)(g_free)));
  }

  g_free(nickname_down);

	return handle;
}

TpHandle idle_handle_for_room(TpHandleRepoIface *storage, const char *channel)
{
	TpHandle handle;
  gchar *channel_down;

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

  channel_down = g_utf8_strdown(channel, -1);
	
  handle = tp_handle_request(storage, channel_down, FALSE);

  if (!handle)
  {
    handle = tp_handle_request(storage, channel_down, TRUE);
    g_assert(tp_handle_set_qdata(storage, handle, idle_handle_real_quark(), g_strdup(channel), (GDestroyNotify)(g_free)));
  }

  g_free(channel_down);

	return handle;
}

gboolean idle_handle_set_presence(TpHandleRepoIface *storage, TpHandle handle, IdleContactPresence *cp)
{
  g_assert(storage != NULL);

  return tp_handle_set_qdata(storage, handle, idle_handle_presence_quark(), cp, (GDestroyNotify)(idle_contact_presence_free));
}

IdleContactPresence *idle_handle_get_presence(TpHandleRepoIface *storage, TpHandle handle)
{
	g_assert(storage != NULL);

  return tp_handle_get_qdata(storage, handle, idle_handle_presence_quark());
}

