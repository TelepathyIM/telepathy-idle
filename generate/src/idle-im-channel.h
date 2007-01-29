/*
 * idle-im-channel.h - Header for IdleIMChannel
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

#ifndef __IDLE_IM_CHANNEL_H__
#define __IDLE_IM_CHANNEL_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _IdleIMChannel IdleIMChannel;
typedef struct _IdleIMChannelClass IdleIMChannelClass;

struct _IdleIMChannelClass {
    GObjectClass parent_class;
};

struct _IdleIMChannel {
    GObject parent;
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


gboolean idle_im_channel_acknowledge_pending_message (IdleIMChannel *obj, guint id, GError **error);
gboolean idle_im_channel_close (IdleIMChannel *obj, GError **error);
gboolean idle_im_channel_get_channel_type (IdleIMChannel *obj, gchar ** ret, GError **error);
gboolean idle_im_channel_get_handle (IdleIMChannel *obj, guint* ret, guint* ret1, GError **error);
gboolean idle_im_channel_get_interfaces (IdleIMChannel *obj, gchar *** ret, GError **error);
gboolean idle_im_channel_list_pending_messages (IdleIMChannel *obj, GPtrArray ** ret, GError **error);
gboolean idle_im_channel_send (IdleIMChannel *obj, guint type, const gchar * text, GError **error);


G_END_DECLS

#endif /* #ifndef __IDLE_IM_CHANNEL_H__*/
