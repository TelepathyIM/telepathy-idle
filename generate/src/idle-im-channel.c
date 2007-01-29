/*
 * idle-im-channel.c - Source for IdleIMChannel
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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "idle-im-channel.h"
#include "idle-im-channel-signals-marshal.h"

#include "idle-im-channel-glue.h"

G_DEFINE_TYPE(IdleIMChannel, idle_im_channel, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CLOSED,
    RECEIVED,
    SENT,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _IdleIMChannelPrivate IdleIMChannelPrivate;

struct _IdleIMChannelPrivate
{
  gboolean dispose_has_run;
};

#define IDLE_IM_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), IDLE_TYPE_IM_CHANNEL, IdleIMChannelPrivate))

static void
idle_im_channel_init (IdleIMChannel *obj)
{
  IdleIMChannelPrivate *priv = IDLE_IM_CHANNEL_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
}

static void idle_im_channel_dispose (GObject *object);
static void idle_im_channel_finalize (GObject *object);

static void
idle_im_channel_class_init (IdleIMChannelClass *idle_im_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (idle_im_channel_class);

  g_type_class_add_private (idle_im_channel_class, sizeof (IdleIMChannelPrivate));

  object_class->dispose = idle_im_channel_dispose;
  object_class->finalize = idle_im_channel_finalize;

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (idle_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_im_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[RECEIVED] =
    g_signal_new ("received",
                  G_OBJECT_CLASS_TYPE (idle_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_im_channel_marshal_VOID__INT_INT_INT_INT_STRING,
                  G_TYPE_NONE, 5, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SENT] =
    g_signal_new ("sent",
                  G_OBJECT_CLASS_TYPE (idle_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_im_channel_marshal_VOID__INT_INT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (idle_im_channel_class), &dbus_glib_idle_im_channel_object_info);
}

void
idle_im_channel_dispose (GObject *object)
{
  IdleIMChannel *self = IDLE_IM_CHANNEL (object);
  IdleIMChannelPrivate *priv = IDLE_IM_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (idle_im_channel_parent_class)->dispose)
    G_OBJECT_CLASS (idle_im_channel_parent_class)->dispose (object);
}

void
idle_im_channel_finalize (GObject *object)
{
  IdleIMChannel *self = IDLE_IM_CHANNEL (object);
  IdleIMChannelPrivate *priv = IDLE_IM_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (idle_im_channel_parent_class)->finalize (object);
}



/**
 * idle_im_channel_acknowledge_pending_message
 *
 * Implements DBus method AcknowledgePendingMessage
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_im_channel_acknowledge_pending_message (IdleIMChannel *obj, guint id, GError **error)
{
  return TRUE;
}


/**
 * idle_im_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_im_channel_close (IdleIMChannel *obj, GError **error)
{
  return TRUE;
}


/**
 * idle_im_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_im_channel_get_channel_type (IdleIMChannel *obj, gchar ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_im_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_im_channel_get_handle (IdleIMChannel *obj, guint* ret, guint* ret1, GError **error)
{
  return TRUE;
}


/**
 * idle_im_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_im_channel_get_interfaces (IdleIMChannel *obj, gchar *** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_im_channel_list_pending_messages
 *
 * Implements DBus method ListPendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_im_channel_list_pending_messages (IdleIMChannel *obj, GPtrArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_im_channel_send
 *
 * Implements DBus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_im_channel_send (IdleIMChannel *obj, guint type, const gchar * text, GError **error)
{
  return TRUE;
}

