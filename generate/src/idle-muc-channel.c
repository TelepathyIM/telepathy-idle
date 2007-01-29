/*
 * idle-muc-channel.c - Source for IdleMUCChannel
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

#include "idle-muc-channel.h"
#include "idle-muc-channel-signals-marshal.h"

#include "idle-muc-channel-glue.h"

G_DEFINE_TYPE(IdleMUCChannel, idle_muc_channel, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CLOSED,
    GROUP_FLAGS_CHANGED,
    LOST_MESSAGE,
    MEMBERS_CHANGED,
    PASSWORD_FLAGS_CHANGED,
    PROPERTIES_CHANGED,
    PROPERTY_FLAGS_CHANGED,
    RECEIVED,
    SEND_ERROR,
    SENT,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _IdleMUCChannelPrivate IdleMUCChannelPrivate;

struct _IdleMUCChannelPrivate
{
  gboolean dispose_has_run;
};

#define IDLE_MUC_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), IDLE_TYPE_MUC_CHANNEL, IdleMUCChannelPrivate))

static void
idle_muc_channel_init (IdleMUCChannel *obj)
{
  IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
}

static void idle_muc_channel_dispose (GObject *object);
static void idle_muc_channel_finalize (GObject *object);

static void
idle_muc_channel_class_init (IdleMUCChannelClass *idle_muc_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (idle_muc_channel_class);

  g_type_class_add_private (idle_muc_channel_class, sizeof (IdleMUCChannelPrivate));

  object_class->dispose = idle_muc_channel_dispose;
  object_class->finalize = idle_muc_channel_finalize;

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (idle_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_muc_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[GROUP_FLAGS_CHANGED] =
    g_signal_new ("group-flags-changed",
                  G_OBJECT_CLASS_TYPE (idle_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_muc_channel_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[LOST_MESSAGE] =
    g_signal_new ("lost-message",
                  G_OBJECT_CLASS_TYPE (idle_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_muc_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[MEMBERS_CHANGED] =
    g_signal_new ("members-changed",
                  G_OBJECT_CLASS_TYPE (idle_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_muc_channel_marshal_VOID__STRING_BOXED_BOXED_BOXED_BOXED,
                  G_TYPE_NONE, 5, G_TYPE_STRING, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY);

  signals[PASSWORD_FLAGS_CHANGED] =
    g_signal_new ("password-flags-changed",
                  G_OBJECT_CLASS_TYPE (idle_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_muc_channel_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[PROPERTIES_CHANGED] =
    g_signal_new ("properties-changed",
                  G_OBJECT_CLASS_TYPE (idle_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_muc_channel_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_VALUE, G_TYPE_INVALID)))));

  signals[PROPERTY_FLAGS_CHANGED] =
    g_signal_new ("property-flags-changed",
                  G_OBJECT_CLASS_TYPE (idle_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_muc_channel_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID)))));

  signals[RECEIVED] =
    g_signal_new ("received",
                  G_OBJECT_CLASS_TYPE (idle_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_muc_channel_marshal_VOID__INT_INT_INT_INT_STRING,
                  G_TYPE_NONE, 5, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SEND_ERROR] =
    g_signal_new ("send-error",
                  G_OBJECT_CLASS_TYPE (idle_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_muc_channel_marshal_VOID__INT_INT_INT_STRING,
                  G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SENT] =
    g_signal_new ("sent",
                  G_OBJECT_CLASS_TYPE (idle_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_muc_channel_marshal_VOID__INT_INT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (idle_muc_channel_class), &dbus_glib_idle_muc_channel_object_info);
}

void
idle_muc_channel_dispose (GObject *object)
{
  IdleMUCChannel *self = IDLE_MUC_CHANNEL (object);
  IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (idle_muc_channel_parent_class)->dispose)
    G_OBJECT_CLASS (idle_muc_channel_parent_class)->dispose (object);
}

void
idle_muc_channel_finalize (GObject *object)
{
  IdleMUCChannel *self = IDLE_MUC_CHANNEL (object);
  IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (idle_muc_channel_parent_class)->finalize (object);
}



/**
 * idle_muc_channel_acknowledge_pending_message
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
gboolean idle_muc_channel_acknowledge_pending_message (IdleMUCChannel *obj, guint id, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_add_members
 *
 * Implements DBus method AddMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_muc_channel_add_members (IdleMUCChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_close
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
gboolean idle_muc_channel_close (IdleMUCChannel *obj, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_get_all_members
 *
 * Implements DBus method GetAllMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_muc_channel_get_all_members (IdleMUCChannel *obj, GArray ** ret, GArray ** ret1, GArray ** ret2, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_get_channel_type
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
gboolean idle_muc_channel_get_channel_type (IdleMUCChannel *obj, gchar ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_get_group_flags
 *
 * Implements DBus method GetGroupFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_muc_channel_get_group_flags (IdleMUCChannel *obj, guint* ret, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_get_handle
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
gboolean idle_muc_channel_get_handle (IdleMUCChannel *obj, guint* ret, guint* ret1, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_get_handle_owners
 *
 * Implements DBus method GetHandleOwners
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_muc_channel_get_handle_owners (IdleMUCChannel *obj, const GArray * handles, GArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_get_interfaces
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
gboolean idle_muc_channel_get_interfaces (IdleMUCChannel *obj, gchar *** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_get_local_pending_members
 *
 * Implements DBus method GetLocalPendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_muc_channel_get_local_pending_members (IdleMUCChannel *obj, GArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_get_members
 *
 * Implements DBus method GetMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_muc_channel_get_members (IdleMUCChannel *obj, GArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_get_message_types
 *
 * Implements DBus method GetMessageTypes
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_muc_channel_get_message_types (IdleMUCChannel *obj, GArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_get_password_flags
 *
 * Implements DBus method GetPasswordFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_muc_channel_get_password_flags (IdleMUCChannel *obj, guint* ret, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_get_properties
 *
 * Implements DBus method GetProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_muc_channel_get_properties (IdleMUCChannel *obj, const GArray * properties, GPtrArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_get_remote_pending_members
 *
 * Implements DBus method GetRemotePendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_muc_channel_get_remote_pending_members (IdleMUCChannel *obj, GArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_get_self_handle
 *
 * Implements DBus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_muc_channel_get_self_handle (IdleMUCChannel *obj, guint* ret, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_list_pending_messages
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
gboolean idle_muc_channel_list_pending_messages (IdleMUCChannel *obj, GPtrArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_list_properties
 *
 * Implements DBus method ListProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_muc_channel_list_properties (IdleMUCChannel *obj, GPtrArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_provide_password
 *
 * Implements DBus method ProvidePassword
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean idle_muc_channel_provide_password (IdleMUCChannel *obj, const gchar * password, DBusGMethodInvocation *context)
{
  return TRUE;
}


/**
 * idle_muc_channel_remove_members
 *
 * Implements DBus method RemoveMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_muc_channel_remove_members (IdleMUCChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_send
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
gboolean idle_muc_channel_send (IdleMUCChannel *obj, guint type, const gchar * text, GError **error)
{
  return TRUE;
}


/**
 * idle_muc_channel_set_properties
 *
 * Implements DBus method SetProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_muc_channel_set_properties (IdleMUCChannel *obj, const GPtrArray * properties, GError **error)
{
  return TRUE;
}

