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

#define __USE_GNU
#include <string.h>

#include "idle-server-connection-util.h"

void msg_split(gchar *msg, gchar *split_buf, gsize max_len, message_cb cb, gpointer user_data)
{
	int i;
	int lasti = 0;
	gchar *tmp;
	gboolean line_ends = FALSE;
	guint len;

	g_assert(msg != NULL);
	g_assert(split_buf != NULL);
	g_assert(cb != NULL);

	len = strnlen(msg, max_len);

	for (i = 0; i < len; i++)
	{
		if ((msg[i] == '\n' || msg[i] == '\r'))
		{
			msg[i] = '\0';
			
			if (i>lasti)
			{
				if ((lasti == 0) && (split_buf[0] != '\0'))
				{
					tmp = g_strconcat(split_buf, msg, NULL);
					memset(split_buf, '\0', max_len);
				}
				else
				{
					tmp = g_strdup(msg+lasti);
				}

				cb(tmp, user_data);

				g_free(tmp);
			}
			
			lasti = i+1;

			line_ends = TRUE;
		}
		else
		{
			line_ends = FALSE;
		}
	}

	if (!line_ends)
	{
		g_strlcpy(split_buf, msg+lasti, max_len-lasti);
	}
	else
	{
		memset(split_buf, '\0', max_len);
	}
}

void idle_output_pending_msg_free(IdleOutputPendingMsg *msg)
{
	g_free(msg->message);
	g_slice_free(IdleOutputPendingMsg, msg);
}

gint pending_msg_compare(gconstpointer a, gconstpointer b, gpointer unused)
{
	const IdleOutputPendingMsg *msg1 = a, *msg2 = b;

	if (msg1->priority < msg2->priority)
	{
		return 1;
	}
	else
	{
		return -1;
	}
}

