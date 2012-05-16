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

#ifndef __IDLE_MUC_CHANNEL_H__
#define __IDLE_MUC_CHANNEL_H__

#include <glib-object.h>
#include <gio/gio.h>

#include <telepathy-glib/base-channel.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/handle.h>
#include <telepathy-glib/message-mixin.h>
#include <telepathy-glib/dbus-properties-mixin.h>

#include "idle-connection.h"

G_BEGIN_DECLS

typedef struct _IdleMUCChannel IdleMUCChannel;
typedef struct _IdleMUCChannelClass IdleMUCChannelClass;
typedef struct _IdleMUCChannelPrivate IdleMUCChannelPrivate;

struct _IdleMUCChannelClass {
	TpBaseChannelClass parent_class;

	TpGroupMixinClass group_class;
};

struct _IdleMUCChannel {
	TpBaseChannel parent;

	TpGroupMixin group;
	TpMessageMixin message_mixin;

	IdleMUCChannelPrivate *priv;
};

typedef enum {
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

IdleMUCChannel *idle_muc_channel_new(IdleConnection *conn, TpHandle handle, TpHandle initiator, gboolean requested);

void idle_muc_channel_badchannelkey(IdleMUCChannel *chan);
void idle_muc_channel_invited(IdleMUCChannel *chan, TpHandle inviter);
gboolean idle_muc_channel_is_modechar(char c);
gboolean idle_muc_channel_is_typechar(char c);
void idle_muc_channel_join(IdleMUCChannel *chan, TpHandle joiner);
void idle_muc_channel_join_attempt(IdleMUCChannel *chan);
void idle_muc_channel_join_error(IdleMUCChannel *chan, IdleMUCChannelJoinError err);
void idle_muc_channel_kick(IdleMUCChannel *chan, TpHandle kicked, TpHandle kicker, const gchar *message);
void idle_muc_channel_mode(IdleMUCChannel *chan, GValueArray *args);
void idle_muc_channel_namereply(IdleMUCChannel *chan, GValueArray *args);
void idle_muc_channel_namereply_end(IdleMUCChannel *chan);
void idle_muc_channel_part(IdleMUCChannel *chan, TpHandle leaver, const gchar *message);
void idle_muc_channel_quit(IdleMUCChannel *chan, TpHandle handle, const gchar *message);
gboolean idle_muc_channel_receive(IdleMUCChannel *chan, TpChannelTextMessageType type, TpHandle sender, const gchar *msg);
void idle_muc_channel_rename(IdleMUCChannel *chan, TpHandle old_handle, TpHandle new_handle);
void idle_muc_channel_topic(IdleMUCChannel *chan, const gchar *topic);
void idle_muc_channel_topic_full(IdleMUCChannel *chan, const TpHandle handle, const gint64 timestamp, const gchar *topic);
void idle_muc_channel_topic_touch(IdleMUCChannel *chan, const TpHandle handle, const gint64 timestamp);
void idle_muc_channel_topic_unset(IdleMUCChannel *chan);

gboolean idle_muc_channel_is_ready(IdleMUCChannel *chan);

G_END_DECLS

#endif /* #ifndef __IDLE_MUC_CHANNEL_H__*/
