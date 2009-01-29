/*
 * This file is part of telepathy-idle
 *
 * Copyright (C) 2009 Collabora Limited
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
 *
 * Authors:
 *  Jonathon Jongsma <jonathon.jongsma@collabora.co.uk>
 */

#include "idle-roomlist-channel.h"

#include <time.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/gtypes.h>

/* FIXME: add new debug flag? */
#define IDLE_DEBUG_FLAG IDLE_DEBUG_CONNECTION
#include "idle-connection.h"
#include "idle-debug.h"
#include "idle-text.h"

static void _channel_iface_init (gpointer, gpointer);
static void _roomlist_iface_init (gpointer, gpointer);
static void connection_status_changed_cb (IdleConnection* conn, guint status, guint reason, IdleRoomlistChannel *self);
static IdleParserHandlerResult _rpl_list_handler (IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _rpl_listend_handler (IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);

G_DEFINE_TYPE_WITH_CODE (IdleRoomlistChannel, idle_roomlist_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES, tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, _channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_ROOM_LIST, _roomlist_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);)

/* property enum */
enum {
    PROP_CONNECTION = 1,
    PROP_OBJECT_PATH,
    PROP_CHANNEL_TYPE,
    PROP_HANDLE_TYPE,
    PROP_HANDLE,
    PROP_CHANNEL_DESTROYED,
    PROP_CHANNEL_PROPERTIES,
    LAST_PROPERTY_ENUM
};


/* private structure */
typedef struct _IdleRoomlistChannelPrivate IdleRoomlistChannelPrivate;

struct _IdleRoomlistChannelPrivate
{
  IdleConnection *connection;
  gchar *object_path;

  GPtrArray *rooms;
  TpHandleSet *handles;

  gboolean listing;
  gboolean closed;
  int status_changed_id;

  gboolean dispose_has_run;
};

#define IDLE_ROOMLIST_CHANNEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), IDLE_TYPE_ROOMLIST_CHANNEL, IdleRoomlistChannelPrivate))

static void
idle_roomlist_channel_init (IdleRoomlistChannel *obj)
{
}

static void idle_roomlist_channel_dispose (GObject *object);
static void idle_roomlist_channel_finalize (GObject *object);

static GObject *
idle_roomlist_channel_constructor (GType type,
                             guint n_props,
                             GObjectConstructParam *props)
{
  GObject *obj;
  IdleRoomlistChannelPrivate *priv;
  TpDBusDaemon *bus;

  obj = G_OBJECT_CLASS (idle_roomlist_channel_parent_class)->constructor (type, n_props, props);
  priv = IDLE_ROOMLIST_CHANNEL_GET_PRIVATE (IDLE_ROOMLIST_CHANNEL (obj));

  bus = tp_dbus_daemon_dup (NULL);
  tp_dbus_daemon_register_object (bus, priv->object_path, obj);
  g_object_unref (bus);

  g_return_val_if_fail (priv->connection, obj);

  priv->status_changed_id = g_signal_connect (priv->connection,
      "status-changed", (GCallback)
      connection_status_changed_cb,
      obj);
  idle_parser_add_handler (priv->connection->parser, IDLE_PARSER_NUMERIC_LIST,
      _rpl_list_handler, obj);
  idle_parser_add_handler (priv->connection->parser,
      IDLE_PARSER_NUMERIC_LISTEND, _rpl_listend_handler, obj);

  priv->rooms = g_ptr_array_new ();
  TpHandleRepoIface *room_handles = tp_base_connection_get_handles (
     TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_ROOM);
  priv->handles = tp_handle_set_new(room_handles);

  return obj;
}

static void
idle_roomlist_channel_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
  IdleRoomlistChannel *chan;
  IdleRoomlistChannelPrivate *priv;

  g_assert (object != NULL);
  g_assert (IDLE_IS_ROOMLIST_CHANNEL (object));

  chan = IDLE_ROOMLIST_CHANNEL (object);
  priv = IDLE_ROOMLIST_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->connection);
      break;

    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;

    case PROP_CHANNEL_TYPE:
      g_value_set_string (value, TP_IFACE_CHANNEL_TYPE_ROOM_LIST);
      break;

    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_NONE);
      break;

    case PROP_HANDLE:
      g_value_set_uint (value, 0);
      break;

    case PROP_CHANNEL_DESTROYED:
      /* TODO: this should be FALSE if there are still pending messages, so
       *       the channel manager can respawn the channel.
       */
      g_value_set_boolean (value, TRUE);
      break;

    case PROP_CHANNEL_PROPERTIES:
      g_value_take_boxed (value,
          tp_dbus_properties_mixin_make_properties_hash (object,
            TP_IFACE_CHANNEL, "TargetHandle",
            TP_IFACE_CHANNEL, "TargetHandleType",
            TP_IFACE_CHANNEL, "ChannelType",
            NULL));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
idle_roomlist_channel_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
  IdleRoomlistChannel *chan = IDLE_ROOMLIST_CHANNEL (object);
  IdleRoomlistChannelPrivate *priv;

  g_assert (chan != NULL);
  g_assert (IDLE_IS_ROOMLIST_CHANNEL (chan));

  priv = IDLE_ROOMLIST_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;

    case PROP_OBJECT_PATH:
      if (priv->object_path)
        g_free (priv->object_path);

      priv->object_path = g_value_dup_string (value);
      break;

    case PROP_HANDLE:
    case PROP_CHANNEL_TYPE:
    case PROP_HANDLE_TYPE:
      /* writeable in the interface, but setting them makes
         no sense, so ignore them */
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
idle_roomlist_channel_class_init (IdleRoomlistChannelClass *idle_roomlist_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (idle_roomlist_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (idle_roomlist_channel_class, sizeof (IdleRoomlistChannelPrivate));

  object_class->constructor = idle_roomlist_channel_constructor;

  object_class->get_property = idle_roomlist_channel_get_property;
  object_class->set_property = idle_roomlist_channel_set_property;

  object_class->dispose = idle_roomlist_channel_dispose;
  object_class->finalize = idle_roomlist_channel_finalize;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH, "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE, "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE, "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");
  g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED, "channel-destroyed");
  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES, "channel-properties");

  param_spec = g_param_spec_object ("connection", "IdleConnection object",
      "The IdleConnection object that owns this "
      "RoomlistChannel object.",
      IDLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  static TpDBusPropertiesMixinPropImpl channel_props[] = {
        { "TargetHandleType", "handle-type", NULL },
        { "TargetHandle", "handle", NULL },
        { "ChannelType", "channel-type", NULL },
        { NULL }
  };

  static TpDBusPropertiesMixinPropImpl roomlist_props[] = {
      { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
        { TP_IFACE_CHANNEL,
          tp_dbus_properties_mixin_getter_gobject_properties,
          NULL,
          channel_props,
        },
        { TP_IFACE_CHANNEL_TYPE_ROOM_LIST,
          tp_dbus_properties_mixin_getter_gobject_properties,
          NULL,
          roomlist_props,
        },
        { NULL }
  };

  idle_roomlist_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class, G_STRUCT_OFFSET (IdleRoomlistChannelClass, dbus_props_class));
}


void
idle_roomlist_channel_dispose (GObject *object)
{
  IdleRoomlistChannel *self = IDLE_ROOMLIST_CHANNEL (object);
  IdleRoomlistChannelPrivate *priv = IDLE_ROOMLIST_CHANNEL_GET_PRIVATE (self);

  g_assert (object != NULL);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->connection, priv->status_changed_id);
      priv->status_changed_id = 0;
    }

  if (!priv->closed)
    {
      priv->closed = TRUE;
      tp_svc_channel_emit_closed ((TpSvcChannel *)(self));
    }

  if (priv->rooms)
    {
      g_ptr_array_free (priv->rooms, TRUE);
      priv->rooms = NULL;
    }

  if (G_OBJECT_CLASS (idle_roomlist_channel_parent_class)->dispose)
    G_OBJECT_CLASS (idle_roomlist_channel_parent_class)->dispose (object);
}


void
idle_roomlist_channel_finalize (GObject *object)
{
  IdleRoomlistChannel *self = IDLE_ROOMLIST_CHANNEL (object);
  IdleRoomlistChannelPrivate *priv = IDLE_ROOMLIST_CHANNEL_GET_PRIVATE (self);

  if (priv->object_path)
    {
      g_free (priv->object_path);
    }

  if (priv->handles)
    {
      tp_handle_set_destroy (priv->handles);
    }

  G_OBJECT_CLASS (idle_roomlist_channel_parent_class)->finalize (object);
}


/**
 * idle_roomlist_channel_close
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
static void
idle_roomlist_channel_close (TpSvcChannel *iface,
                             DBusGMethodInvocation *context)
{
  IdleRoomlistChannel *obj = IDLE_ROOMLIST_CHANNEL (iface);

  g_assert (obj != NULL);
  g_assert (IDLE_IS_ROOMLIST_CHANNEL (obj));

  g_object_run_dispose (G_OBJECT (iface));

  tp_svc_channel_return_from_close (context);
}


/**
 * idle_roomlist_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
idle_roomlist_channel_get_channel_type (TpSvcChannel *iface,
                                        DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_ROOM_LIST);
}


/**
 * idle_roomlist_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
idle_roomlist_channel_get_handle (TpSvcChannel *iface,
                                  DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_handle (context, 0, 0);
}


/**
 * idle_roomlist_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
idle_roomlist_channel_get_interfaces (TpSvcChannel *iface,
                                      DBusGMethodInvocation *context)
{
  const gchar *interfaces[] = {NULL};
  tp_svc_channel_return_from_get_interfaces (context, interfaces);
}


/**
 * idle_roomlist_channel_get_listing_rooms
 *
 * Implements D-Bus method GetListingRooms
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 */
static void
idle_roomlist_channel_get_listing_rooms (TpSvcChannelTypeRoomList *iface,
                                         DBusGMethodInvocation *context)
{
  IdleRoomlistChannel *self = IDLE_ROOMLIST_CHANNEL (iface);
  IdleRoomlistChannelPrivate *priv;

  g_assert (IDLE_IS_ROOMLIST_CHANNEL (self));

  priv = IDLE_ROOMLIST_CHANNEL_GET_PRIVATE (self);

  tp_svc_channel_type_room_list_return_from_get_listing_rooms (
      context, priv->listing);
}


/**
 * idle_roomlist_channel_list_rooms
 *
 * Implements D-Bus method ListRooms
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 */
static void
idle_roomlist_channel_list_rooms (TpSvcChannelTypeRoomList *iface,
                                  DBusGMethodInvocation *context)
{
  IdleRoomlistChannel *self = IDLE_ROOMLIST_CHANNEL (iface);
  IdleRoomlistChannelPrivate *priv;

  g_assert (IDLE_IS_ROOMLIST_CHANNEL (self));

  priv = IDLE_ROOMLIST_CHANNEL_GET_PRIVATE (self);

  priv->listing = TRUE;
  tp_svc_channel_type_room_list_emit_listing_rooms (iface, TRUE);

  idle_connection_send(priv->connection, "LIST");

  tp_svc_channel_type_room_list_return_from_list_rooms (context);
}

/**
 * idle_roomlist_channel_stop_listing
 *
 * Implements D-Bus method StopListing
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 */
static void
idle_roomlist_channel_stop_listing (TpSvcChannelTypeRoomList *iface,
                                    DBusGMethodInvocation *context)
{
  IdleRoomlistChannel *self = IDLE_ROOMLIST_CHANNEL (iface);

  g_assert (IDLE_IS_ROOMLIST_CHANNEL (self));

  GError *error = g_error_new (TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED, "Room listing not yet implemented");
  dbus_g_method_return_error (context, error);
  g_error_free (error);

  /*
  priv->listing = FALSE;
  tp_svc_channel_type_room_list_emit_listing_rooms (iface, FALSE);

  stop_listing (self);

  tp_svc_channel_type_room_list_return_from_stop_listing (context);
  */
}


static void
_channel_iface_init (gpointer g_iface,
                     gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, idle_roomlist_channel_##x)
  IMPLEMENT (close);
  IMPLEMENT (get_channel_type);
  IMPLEMENT (get_handle);
  IMPLEMENT (get_interfaces);
#undef IMPLEMENT
}


static void
_roomlist_iface_init (gpointer g_iface,
                  gpointer iface_data)
{
  TpSvcChannelTypeRoomListClass *klass =
    (TpSvcChannelTypeRoomListClass *)(g_iface);

#define IMPLEMENT(x) tp_svc_channel_type_room_list_implement_##x (\
    klass, idle_roomlist_channel_##x)
  IMPLEMENT (get_listing_rooms);
  IMPLEMENT (list_rooms);
  IMPLEMENT (stop_listing);
#undef IMPLEMENT
}


static IdleParserHandlerResult
_rpl_list_handler (IdleParser *parser,
                   IdleParserMessageCode code,
                   GValueArray *args,
                   gpointer user_data)
{
  IdleRoomlistChannel* self = IDLE_ROOMLIST_CHANNEL (user_data);
  IdleRoomlistChannelPrivate *priv = IDLE_ROOMLIST_CHANNEL_GET_PRIVATE (self);
  GValue room = {0,};
  GValue *tmp;
  GHashTable *keys;

  TpHandle room_handle = g_value_get_uint (g_value_array_get_nth (args, 0));
  TpHandleRepoIface *handl_repo =
    tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection),
        TP_HANDLE_TYPE_ROOM);
  const gchar *room_name = tp_handle_inspect(handl_repo, room_handle);
  guint num_users = g_value_get_uint (g_value_array_get_nth (args, 1));
  /* topic is optional */
  const gchar *topic = "";
  if (args->n_values > 2)
    {
      topic = g_value_get_string (g_value_array_get_nth (args, 2));
    }

  keys = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);
  tmp = g_slice_new0 (GValue);
  g_value_init (tmp, G_TYPE_STRING);
  g_value_set_string (tmp, room_name);
  g_hash_table_insert (keys, "name", tmp);

  tmp = g_slice_new0 (GValue);
  g_value_init (tmp, G_TYPE_UINT);
  g_value_set_uint (tmp, num_users);
  g_hash_table_insert (keys, "members", tmp);

  tmp = g_slice_new0 (GValue);
  g_value_init (tmp, G_TYPE_STRING);
  g_value_set_string (tmp, topic);
  g_hash_table_insert (keys, "subject", tmp);

  g_value_init (&room, TP_STRUCT_TYPE_ROOM_INFO);
  g_value_take_boxed (&room,
      dbus_g_type_specialized_construct (TP_STRUCT_TYPE_ROOM_INFO));

  dbus_g_type_struct_set (&room,
      0, room_handle,
      1, "org.freedesktop.Telepathy.Channel.Type.Text",
      2, keys,
      G_MAXUINT);

  IDLE_DEBUG ("adding new room signal data to pending: %s", room_name);
  g_ptr_array_add (priv->rooms, g_value_get_boxed (&room));
  /* retain a ref to the room handle */
  tp_handle_set_add (priv->handles, room_handle);
  g_hash_table_destroy (keys);

  return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}


static gboolean
emit_room_signal (IdleRoomlistChannel *self)
{
  IdleRoomlistChannelPrivate *priv = IDLE_ROOMLIST_CHANNEL_GET_PRIVATE (self);

  if (!priv->listing)
      return FALSE;

  if (priv->rooms->len == 0)
      return TRUE;

  tp_svc_channel_type_room_list_emit_got_rooms (
      (TpSvcChannelTypeRoomList *) self, priv->rooms);

  while (priv->rooms->len != 0)
    {
      gpointer boxed = g_ptr_array_index (priv->rooms, 0);
      g_boxed_free (TP_STRUCT_TYPE_ROOM_INFO, boxed);
      g_ptr_array_remove_index_fast (priv->rooms, 0);
    }

  return TRUE;
}

static IdleParserHandlerResult
_rpl_listend_handler (IdleParser *parser,
                      IdleParserMessageCode code,
                      GValueArray *args,
                      gpointer user_data)
{
  IdleRoomlistChannel* self = IDLE_ROOMLIST_CHANNEL (user_data);
  IdleRoomlistChannelPrivate *priv = IDLE_ROOMLIST_CHANNEL_GET_PRIVATE (self);
  emit_room_signal (self);

  priv->listing = FALSE;
  tp_svc_channel_type_room_list_emit_listing_rooms (
      (TpSvcChannelTypeRoomList *) self, FALSE);

  /*
  g_source_remove (priv->timer_source_id);
  priv->timer_source_id = 0;
  */

  return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}


static void
connection_status_changed_cb (IdleConnection* conn,
                              guint status,
                              guint reason,
                              IdleRoomlistChannel *self)
{
  IdleRoomlistChannelPrivate *priv = IDLE_ROOMLIST_CHANNEL_GET_PRIVATE (self);

  if (status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      idle_parser_remove_handlers_by_data (conn->parser, self);
      if (priv->status_changed_id != 0)
        {
          g_signal_handler_disconnect (conn, priv->status_changed_id);
          priv->status_changed_id = 0;
        }
    }
}


