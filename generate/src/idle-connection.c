/*
 * idle-connection.c - Source for IdleConnection
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

#include "idle-connection.h"
#include "idle-connection-signals-marshal.h"

#include "idle-connection-glue.h"

G_DEFINE_TYPE(IdleConnection, idle_connection, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CAPABILITIES_CHANGED,
    NEW_CHANNEL,
    PRESENCE_UPDATE,
    RENAMED,
    STATUS_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _IdleConnectionPrivate IdleConnectionPrivate;

struct _IdleConnectionPrivate
{
  gboolean dispose_has_run;
};

#define IDLE_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), IDLE_TYPE_CONNECTION, IdleConnectionPrivate))

static void
idle_connection_init (IdleConnection *obj)
{
  IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
}

static void idle_connection_dispose (GObject *object);
static void idle_connection_finalize (GObject *object);

static void
idle_connection_class_init (IdleConnectionClass *idle_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (idle_connection_class);

  g_type_class_add_private (idle_connection_class, sizeof (IdleConnectionPrivate));

  object_class->dispose = idle_connection_dispose;
  object_class->finalize = idle_connection_finalize;

  signals[CAPABILITIES_CHANGED] =
    g_signal_new ("capabilities-changed",
                  G_OBJECT_CLASS_TYPE (idle_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_connection_marshal_VOID__INT_BOXED_BOXED,
                  G_TYPE_NONE, 3, G_TYPE_UINT, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID)))), (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID)))));

  signals[NEW_CHANNEL] =
    g_signal_new ("new-channel",
                  G_OBJECT_CLASS_TYPE (idle_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_connection_marshal_VOID__STRING_STRING_INT_INT_BOOLEAN,
                  G_TYPE_NONE, 5, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_BOOLEAN);

  signals[PRESENCE_UPDATE] =
    g_signal_new ("presence-update",
                  G_OBJECT_CLASS_TYPE (idle_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_connection_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_map ("GHashTable", G_TYPE_UINT, (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)))), G_TYPE_INVALID)))));

  signals[RENAMED] =
    g_signal_new ("renamed",
                  G_OBJECT_CLASS_TYPE (idle_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_connection_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[STATUS_CHANGED] =
    g_signal_new ("status-changed",
                  G_OBJECT_CLASS_TYPE (idle_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_connection_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (idle_connection_class), &dbus_glib_idle_connection_object_info);
}

void
idle_connection_dispose (GObject *object)
{
  IdleConnection *self = IDLE_CONNECTION (object);
  IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (idle_connection_parent_class)->dispose)
    G_OBJECT_CLASS (idle_connection_parent_class)->dispose (object);
}

void
idle_connection_finalize (GObject *object)
{
  IdleConnection *self = IDLE_CONNECTION (object);
  IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (idle_connection_parent_class)->finalize (object);
}



/**
 * idle_connection_add_status
 *
 * Implements DBus method AddStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_add_status (IdleConnection *obj, const gchar * status, GHashTable * parms, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_advertise_capabilities
 *
 * Implements DBus method AdvertiseCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_advertise_capabilities (IdleConnection *obj, const gchar ** add, const gchar ** remove, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_clear_status
 *
 * Implements DBus method ClearStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_clear_status (IdleConnection *obj, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_disconnect
 *
 * Implements DBus method Disconnect
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_disconnect (IdleConnection *obj, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_get_capabilities
 *
 * Implements DBus method GetCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_get_capabilities (IdleConnection *obj, guint handle, GPtrArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_get_interfaces (IdleConnection *obj, gchar *** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_get_protocol
 *
 * Implements DBus method GetProtocol
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_get_protocol (IdleConnection *obj, gchar ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_get_self_handle
 *
 * Implements DBus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_get_self_handle (IdleConnection *obj, guint* ret, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_get_status
 *
 * Implements DBus method GetStatus
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_get_status (IdleConnection *obj, guint* ret, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_get_statuses
 *
 * Implements DBus method GetStatuses
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_get_statuses (IdleConnection *obj, GHashTable ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_hold_handle
 *
 * Implements DBus method HoldHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean idle_connection_hold_handle (IdleConnection *obj, guint handle_type, guint handle, DBusGMethodInvocation *context)
{
  return TRUE;
}


/**
 * idle_connection_inspect_handle
 *
 * Implements DBus method InspectHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_inspect_handle (IdleConnection *obj, guint handle_type, guint handle, gchar ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_list_channels
 *
 * Implements DBus method ListChannels
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_list_channels (IdleConnection *obj, GPtrArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_release_handle
 *
 * Implements DBus method ReleaseHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean idle_connection_release_handle (IdleConnection *obj, guint handle_type, guint handle, DBusGMethodInvocation *context)
{
  return TRUE;
}


/**
 * idle_connection_remove_status
 *
 * Implements DBus method RemoveStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_remove_status (IdleConnection *obj, const gchar * status, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_request_channel
 *
 * Implements DBus method RequestChannel
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_request_channel (IdleConnection *obj, const gchar * type, guint handle_type, guint handle, gboolean suppress_handler, gchar ** ret, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_request_handle
 *
 * Implements DBus method RequestHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean idle_connection_request_handle (IdleConnection *obj, guint handle_type, const gchar * name, DBusGMethodInvocation *context)
{
  return TRUE;
}


/**
 * idle_connection_request_presence
 *
 * Implements DBus method RequestPresence
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_request_presence (IdleConnection *obj, const GArray * contacts, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_request_rename
 *
 * Implements DBus method RequestRename
 * on interface org.freedesktop.Telepathy.Connection.Interface.Renaming
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_request_rename (IdleConnection *obj, const gchar * name, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_set_last_activity_time
 *
 * Implements DBus method SetLastActivityTime
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_set_last_activity_time (IdleConnection *obj, guint time, GError **error)
{
  return TRUE;
}


/**
 * idle_connection_set_status
 *
 * Implements DBus method SetStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_set_status (IdleConnection *obj, GHashTable * statuses, GError **error)
{
  return TRUE;
}

