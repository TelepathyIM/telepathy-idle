/*
 * idle-muc-channel.h - Header for IdleMUCChannel
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

G_BEGIN_DECLS

typedef struct _IdleMUCChannel IdleMUCChannel;
typedef struct _IdleMUCChannelClass IdleMUCChannelClass;

struct _IdleMUCChannelClass {
    GObjectClass parent_class;
};

struct _IdleMUCChannel {
    GObject parent;
};

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


gboolean idle_muc_channel_acknowledge_pending_message (IdleMUCChannel *obj, guint id, GError **error);
gboolean idle_muc_channel_add_members (IdleMUCChannel *obj, const GArray * contacts, const gchar * message, GError **error);
gboolean idle_muc_channel_close (IdleMUCChannel *obj, GError **error);
gboolean idle_muc_channel_get_all_members (IdleMUCChannel *obj, GArray ** ret, GArray ** ret1, GArray ** ret2, GError **error);
gboolean idle_muc_channel_get_channel_type (IdleMUCChannel *obj, gchar ** ret, GError **error);
gboolean idle_muc_channel_get_group_flags (IdleMUCChannel *obj, guint* ret, GError **error);
gboolean idle_muc_channel_get_handle (IdleMUCChannel *obj, guint* ret, guint* ret1, GError **error);
gboolean idle_muc_channel_get_handle_owners (IdleMUCChannel *obj, const GArray * handles, GArray ** ret, GError **error);
gboolean idle_muc_channel_get_interfaces (IdleMUCChannel *obj, gchar *** ret, GError **error);
gboolean idle_muc_channel_get_local_pending_members (IdleMUCChannel *obj, GArray ** ret, GError **error);
gboolean idle_muc_channel_get_members (IdleMUCChannel *obj, GArray ** ret, GError **error);
gboolean idle_muc_channel_get_message_types (IdleMUCChannel *obj, GArray ** ret, GError **error);
gboolean idle_muc_channel_get_password_flags (IdleMUCChannel *obj, guint* ret, GError **error);
gboolean idle_muc_channel_get_properties (IdleMUCChannel *obj, const GArray * properties, GPtrArray ** ret, GError **error);
gboolean idle_muc_channel_get_remote_pending_members (IdleMUCChannel *obj, GArray ** ret, GError **error);
gboolean idle_muc_channel_get_self_handle (IdleMUCChannel *obj, guint* ret, GError **error);
gboolean idle_muc_channel_list_pending_messages (IdleMUCChannel *obj, GPtrArray ** ret, GError **error);
gboolean idle_muc_channel_list_properties (IdleMUCChannel *obj, GPtrArray ** ret, GError **error);
gboolean idle_muc_channel_provide_password (IdleMUCChannel *obj, const gchar * password, DBusGMethodInvocation *context);
gboolean idle_muc_channel_remove_members (IdleMUCChannel *obj, const GArray * contacts, const gchar * message, GError **error);
gboolean idle_muc_channel_send (IdleMUCChannel *obj, guint type, const gchar * text, GError **error);
gboolean idle_muc_channel_set_properties (IdleMUCChannel *obj, const GPtrArray * properties, GError **error);


G_END_DECLS

#endif /* #ifndef __IDLE_MUC_CHANNEL_H__*/
