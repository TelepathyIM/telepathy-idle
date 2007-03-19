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

#ifndef __IDLE_MUC_CHANNEL_H__
#define __IDLE_MUC_CHANNEL_H__

#include <glib-object.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/text-mixin.h>

#include "idle-handles.h"

G_BEGIN_DECLS

typedef struct _IdleMUCChannel IdleMUCChannel;
typedef struct _IdleMUCChannelClass IdleMUCChannelClass;

struct _IdleMUCChannelClass {
	GObjectClass parent_class;
	TpGroupMixinClass group_class;
	TpTextMixinClass text_class;
};

struct _IdleMUCChannel {
	GObject parent;
	TpGroupMixin group;
	TpTextMixin text;
};

typedef enum
{
	MUC_CHANNEL_JOIN_ERROR_NONE,
	MUC_CHANNEL_JOIN_ERROR_BANNED,
	MUC_CHANNEL_JOIN_ERROR_INVITE_ONLY,
	MUC_CHANNEL_JOIN_ERROR_FULL
} IdleMUCChannelJoinError;

GType idle_muc_channel_get_type(void);

/* TYPE MACROS */
#define IDLE_TYPE_MUC_CHANNEL \
  (idle_muc_channel_get_type())
#define IDLE_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), IDLE_TYPE_MUC_CHANNEL, IdleMUCChannel))
#define IDLE_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), IDLE_TYPE_MUC_CHANNEL, IdleMUCChannelClass))
#define IDLE_IS_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_MUC_CHANNEL))
#define IDLE_IS_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_MUC_CHANNEL))
#define IDLE_MUC_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), IDLE_TYPE_MUC_CHANNEL, IdleMUCChannelClass))

void _idle_muc_channel_join_attempt(IdleMUCChannel *chan);

gboolean _idle_muc_channel_receive(IdleMUCChannel *chan, TpChannelTextMessageType type, TpHandle sender, const gchar *msg);
void _idle_muc_channel_rename(IdleMUCChannel *chan, guint old, guint _new);

void _idle_muc_channel_join(IdleMUCChannel *chan, const gchar *nick);
void _idle_muc_channel_part(IdleMUCChannel *chan, const gchar *nick);
void _idle_muc_channel_kick(IdleMUCChannel *chan, const gchar *nick, const gchar *kicker, TpChannelGroupChangeReason reason);
void _idle_muc_channel_handle_quit(IdleMUCChannel *chan, TpHandle handle, gboolean suppress, TpHandle actor, TpChannelGroupChangeReason reason);
void _idle_muc_channel_invited(IdleMUCChannel *chan, TpHandle inviter);

void _idle_muc_channel_names(IdleMUCChannel *chan, GArray *nicks);
void _idle_muc_channel_mode(IdleMUCChannel *chan, const gchar *params);
void _idle_muc_channel_topic(IdleMUCChannel *chan, const gchar *topic);
void _idle_muc_channel_topic_touch(IdleMUCChannel *chan, const TpHandle handle, const guint timestamp);
void _idle_muc_channel_topic_full(IdleMUCChannel *chan, const TpHandle handle, const guint timestamp, const gchar *topic);
void _idle_muc_channel_topic_unset(IdleMUCChannel *chan);

void _idle_muc_channel_badchannelkey(IdleMUCChannel *chan);
void _idle_muc_channel_join_error(IdleMUCChannel *chan, IdleMUCChannelJoinError err);

gboolean _idle_muc_channel_has_current_member(IdleMUCChannel *chan, TpHandle handle);

G_END_DECLS

#endif /* #ifndef __IDLE_MUC_CHANNEL_H__*/
