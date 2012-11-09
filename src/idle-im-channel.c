/*
 * This file is part of telepathy-idle
 *
 * Copyright (C) 2006-2007, 2012 Collabora Limited
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

#include "config.h"

#include "idle-im-channel.h"

#include <dbus/dbus-glib.h>

#include <telepathy-glib/interfaces.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_IM
#include "idle-connection.h"
#include "idle-debug.h"
#include "idle-text.h"

static void _destroyable_iface_init(gpointer, gpointer);

static GPtrArray *idle_im_channel_get_interfaces (TpBaseChannel *channel);
static void idle_im_channel_close (TpBaseChannel *base);
static void idle_im_channel_send (GObject *obj, TpMessage *message, TpMessageSendingFlags flags);
static void idle_im_channel_finalize (GObject *object);

G_DEFINE_TYPE_WITH_CODE(IdleIMChannel, idle_im_channel, TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL_TYPE_TEXT, tp_message_mixin_text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MESSAGES, tp_message_mixin_messages_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL_INTERFACE_DESTROYABLE, _destroyable_iface_init);
    )

/* private structure */
typedef struct _IdleIMChannelPrivate IdleIMChannelPrivate;

struct _IdleIMChannelPrivate {
  gpointer unused;
};

static void
idle_im_channel_init (IdleIMChannel *obj)
{
}

static void
idle_im_channel_constructed (GObject *obj)
{
  TpChannelTextMessageType types[] = {
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
  };
  const gchar * supported_content_types[] = {
      "text/plain",
      NULL
  };

  G_OBJECT_CLASS (idle_im_channel_parent_class)->constructed (obj);

  /* initialize message mixin */
  tp_message_mixin_init (obj, G_STRUCT_OFFSET (IdleIMChannel, message_mixin),
      tp_base_channel_get_connection (TP_BASE_CHANNEL (obj)));
  tp_message_mixin_implement_sending (obj, idle_im_channel_send,
      G_N_ELEMENTS (types), types, 0,
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_FAILURES,
      supported_content_types);
}

static void
idle_im_channel_fill_properties (
    TpBaseChannel *chan,
    GHashTable *properties)
{
  TpBaseChannelClass *klass = TP_BASE_CHANNEL_CLASS (idle_im_channel_parent_class);

  klass->fill_immutable_properties (chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessagePartSupportFlags",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "DeliveryReportingSupport",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "SupportedContentTypes",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessageTypes",
      NULL);
}

static gchar *
idle_im_channel_get_path_suffix (TpBaseChannel *base)
{
  return g_strdup_printf("ImChannel%u",
      tp_base_channel_get_target_handle (base));
}

static void
idle_im_channel_class_init (IdleIMChannelClass *idle_im_channel_class)
{
  TpBaseChannelClass *base_class = TP_BASE_CHANNEL_CLASS (idle_im_channel_class);
  GObjectClass *object_class = G_OBJECT_CLASS (idle_im_channel_class);

  g_type_class_add_private (idle_im_channel_class, sizeof (IdleIMChannelPrivate));

  object_class->constructed = idle_im_channel_constructed;
  object_class->finalize = idle_im_channel_finalize;

  base_class->channel_type = TP_IFACE_CHANNEL_TYPE_TEXT;
  base_class->target_handle_type = TP_HANDLE_TYPE_CONTACT;
  base_class->close = idle_im_channel_close;
  base_class->fill_immutable_properties = idle_im_channel_fill_properties;
  base_class->get_object_path_suffix = idle_im_channel_get_path_suffix;
  base_class->get_interfaces = idle_im_channel_get_interfaces;

  tp_message_mixin_init_dbus_properties (object_class);
}

static void
idle_im_channel_finalize (GObject *object)
{
  tp_message_mixin_finalize (object);

  G_OBJECT_CLASS(idle_im_channel_parent_class)->finalize (object);
}

gboolean
idle_im_channel_receive (
    IdleIMChannel *chan,
    TpChannelTextMessageType type,
    TpHandle sender,
    const gchar *text)
{
  TpBaseConnection *base_conn = tp_base_channel_get_connection (TP_BASE_CHANNEL (chan));

  return idle_text_received (G_OBJECT (chan), base_conn, type, text, sender);
}

static void
idle_im_channel_close (TpBaseChannel *base)
{
  IdleIMChannel *obj = IDLE_IM_CHANNEL (base);

  /* The IM manager will resurrect the channel if we have pending
   * messages. When we're resurrected, we want the initiator
   * to be the contact who sent us those messages, if it isn't already */
  if (tp_message_mixin_has_pending_messages ((GObject *)obj, NULL))
    {
      IDLE_DEBUG("%p not really closing, I still have pending messages", obj);

      tp_message_mixin_set_rescued ((GObject *) obj);
      tp_base_channel_reopened (base, tp_base_channel_get_target_handle (base));
    }
  else
    {
      IDLE_DEBUG ("%p actually closing, I have no pending messages", obj);
      tp_base_channel_destroyed (base);
    }
}

static GPtrArray *
idle_im_channel_get_interfaces (TpBaseChannel *channel)
{
  GPtrArray *interfaces =
      TP_BASE_CHANNEL_CLASS (idle_im_channel_parent_class)->get_interfaces (
          channel);

  g_ptr_array_add (interfaces, TP_IFACE_CHANNEL_INTERFACE_MESSAGES);
  g_ptr_array_add (interfaces, TP_IFACE_CHANNEL_INTERFACE_DESTROYABLE);

  return interfaces;
}

/**
 * idle_im_channel_send
 *
 * Indirectly implements (via TpMessageMixin) D-Bus method Send on interface
 * org.freedesktop.Telepathy.Channel.Type.Text and D-Bus method SendMessage on
 * Channel.Interface.Messages
 */
static void
idle_im_channel_send (
    GObject *obj,
    TpMessage *message,
    TpMessageSendingFlags flags)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (obj);
  TpBaseConnection *conn = tp_base_channel_get_connection (base);
  const gchar *recipient = tp_handle_inspect (
      tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT),
      tp_base_channel_get_target_handle (base));

  idle_text_send (obj, message, flags, recipient, IDLE_CONNECTION (conn));
}

static void
idle_im_channel_destroy (
    TpSvcChannelInterfaceDestroyable *iface,
    DBusGMethodInvocation *context)
{
  TpBaseChannel *chan = TP_BASE_CHANNEL (iface);
  GObject *obj = (GObject *) chan;

  IDLE_DEBUG ("called on %p with %spending messages", obj,
      tp_message_mixin_has_pending_messages (obj, NULL) ? "" : "no ");

  tp_message_mixin_clear (obj);
  tp_base_channel_destroyed (chan);

  tp_svc_channel_interface_destroyable_return_from_destroy(context);
}

static void
_destroyable_iface_init (
    gpointer klass,
    gpointer iface_data)
{
#define IMPLEMENT(x) tp_svc_channel_interface_destroyable_implement_##x (\
    klass, idle_im_channel_##x)
  IMPLEMENT (destroy);
#undef IMPLEMENT
}
