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

#ifndef __IDLE_IM_CHANNEL_H__
#define __IDLE_IM_CHANNEL_H__

#include <glib-object.h>
#include <telepathy-glib/message-mixin.h>
#include <telepathy-glib/dbus-properties-mixin.h>
#include <telepathy-glib/base-channel.h>

G_BEGIN_DECLS

typedef struct _IdleIMChannel IdleIMChannel;
typedef struct _IdleIMChannelClass IdleIMChannelClass;

struct _IdleIMChannelClass {
  TpBaseChannelClass parent_class;
};

struct _IdleIMChannel {
  TpBaseChannel parent;
  TpMessageMixin message_mixin;
};

GType idle_im_channel_get_type(void);

/* TYPE MACROS */
#define IDLE_TYPE_IM_CHANNEL \
	(idle_im_channel_get_type())
#define IDLE_IM_CHANNEL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), IDLE_TYPE_IM_CHANNEL, IdleIMChannel))
#define IDLE_IM_CHANNEL_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), IDLE_TYPE_IM_CHANNEL, IdleIMChannelClass))
#define IDLE_IS_IM_CHANNEL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_IM_CHANNEL))
#define IDLE_IS_IM_CHANNEL_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_IM_CHANNEL))
#define IDLE_IM_CHANNEL_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS ((obj), IDLE_TYPE_IM_CHANNEL, IdleIMChannelClass))

gboolean idle_im_channel_receive(IdleIMChannel *chan, TpChannelTextMessageType type, TpHandle sender, const gchar *msg);

G_END_DECLS

#endif /* #ifndef __IDLE_IM_CHANNEL_H__*/

