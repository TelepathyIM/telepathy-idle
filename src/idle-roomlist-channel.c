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

#include <dbus/dbus-glib.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_ROOMLIST
#include "idle-connection.h"
#include "idle-debug.h"
#include "idle-text.h"

static void idle_roomlist_channel_close (TpBaseChannel *channel);
static void _roomlist_iface_init (gpointer, gpointer);
static void connection_status_changed_cb (IdleConnection* conn, guint status, guint reason, IdleRoomlistChannel *self);
static IdleParserHandlerResult _rpl_list_handler (IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _rpl_listend_handler (IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);

G_DEFINE_TYPE_WITH_CODE (IdleRoomlistChannel, idle_roomlist_channel,
    TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_ROOM_LIST, _roomlist_iface_init);
    )

/* private structure */
struct _IdleRoomlistChannelPrivate
{
  IdleConnection *connection;

  GPtrArray *rooms;
  TpHandleSet *handles;

  gboolean listing;
  gboolean closed;
  int status_changed_id;

  gboolean dispose_has_run;
};

static void
idle_roomlist_channel_init (IdleRoomlistChannel *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, IDLE_TYPE_ROOMLIST_CHANNEL,
      IdleRoomlistChannelPrivate);
}

static void idle_roomlist_channel_dispose (GObject *object);
static void idle_roomlist_channel_finalize (GObject *object);

static void
idle_roomlist_channel_constructed (GObject *obj)
{
  IdleRoomlistChannel *self = IDLE_ROOMLIST_CHANNEL (obj);
  IdleRoomlistChannelPrivate *priv = self->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (obj);

  G_OBJECT_CLASS (idle_roomlist_channel_parent_class)->constructed (obj);

  tp_base_channel_register (base);

  priv->connection = IDLE_CONNECTION (tp_base_channel_get_connection (TP_BASE_CHANNEL (obj)));

  priv->status_changed_id = g_signal_connect (priv->connection,
      "status-changed", (GCallback) connection_status_changed_cb,
      obj);
  idle_parser_add_handler (priv->connection->parser, IDLE_PARSER_NUMERIC_LIST,
      _rpl_list_handler, obj);
  idle_parser_add_handler (priv->connection->parser,
      IDLE_PARSER_NUMERIC_LISTEND, _rpl_listend_handler, obj);

  priv->rooms = g_ptr_array_new ();
  priv->handles = tp_handle_set_new(tp_base_connection_get_handles (
     TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_ROOM));
}

static gchar *
idle_roomlist_channel_get_path_suffix (TpBaseChannel *chan)
{
  return g_strdup ("RoomListChannel");
}

static void
idle_roomlist_channel_get_roomlist_property (
    GObject *object,
    GQuark iface,
    GQuark name,
    GValue *value,
    gpointer getter_data)
{
  g_return_if_fail (iface == TP_IFACE_QUARK_CHANNEL_TYPE_ROOM_LIST);
  g_return_if_fail (name == g_quark_from_static_string ("Server"));
  g_return_if_fail (G_VALUE_HOLDS_STRING (value));

  g_value_set_static_string (value, "");
}

static void
idle_roomlist_channel_fill_properties (
    TpBaseChannel *chan,
    GHashTable *properties)
{
  TpBaseChannelClass *klass = TP_BASE_CHANNEL_CLASS (idle_roomlist_channel_parent_class);

  klass->fill_immutable_properties (chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
      TP_IFACE_CHANNEL_TYPE_ROOM_LIST, "Server",
      NULL);
}

static void
idle_roomlist_channel_class_init (IdleRoomlistChannelClass *idle_roomlist_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (idle_roomlist_channel_class);
  TpBaseChannelClass *base_channel_class = TP_BASE_CHANNEL_CLASS (idle_roomlist_channel_class);
  static TpDBusPropertiesMixinPropImpl roomlist_props[] = {
      { "Server", NULL, NULL },
      { NULL }
  };


  g_type_class_add_private (idle_roomlist_channel_class, sizeof (IdleRoomlistChannelPrivate));

  object_class->constructed = idle_roomlist_channel_constructed;
  object_class->dispose = idle_roomlist_channel_dispose;
  object_class->finalize = idle_roomlist_channel_finalize;

  base_channel_class->channel_type = TP_IFACE_CHANNEL_TYPE_ROOM_LIST;
  base_channel_class->target_handle_type = TP_HANDLE_TYPE_NONE;
  base_channel_class->close = idle_roomlist_channel_close;
  base_channel_class->fill_immutable_properties = idle_roomlist_channel_fill_properties;
  base_channel_class->get_object_path_suffix = idle_roomlist_channel_get_path_suffix;

  tp_dbus_properties_mixin_implement_interface (object_class,
      TP_IFACE_QUARK_CHANNEL_TYPE_ROOM_LIST,
      idle_roomlist_channel_get_roomlist_property,
      NULL,
      roomlist_props);
}


void
idle_roomlist_channel_dispose (GObject *object)
{
  IdleRoomlistChannel *self = IDLE_ROOMLIST_CHANNEL (object);
  IdleRoomlistChannelPrivate *priv = self->priv;

  g_assert (object != NULL);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->connection, priv->status_changed_id);
      priv->status_changed_id = 0;
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
  IdleRoomlistChannelPrivate *priv = self->priv;

  if (priv->handles)
    {
      tp_handle_set_destroy (priv->handles);
    }

  G_OBJECT_CLASS (idle_roomlist_channel_parent_class)->finalize (object);
}


static void
idle_roomlist_channel_close (TpBaseChannel *channel)
{
  IdleRoomlistChannel *self = IDLE_ROOMLIST_CHANNEL (channel);
  IdleRoomlistChannelPrivate *priv = self->priv;

  idle_parser_remove_handlers_by_data (priv->connection->parser, channel);
  tp_base_channel_destroyed (channel);
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
  IdleRoomlistChannelPrivate *priv = self->priv;;

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
  IdleRoomlistChannelPrivate *priv = self->priv;

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
  GError error = { TP_ERROR, TP_ERROR_NOT_IMPLEMENTED, "Can't stop listing!" };

  g_assert (IDLE_IS_ROOMLIST_CHANNEL (self));

  dbus_g_method_return_error (context, &error);

  /*
  priv->listing = FALSE;
  tp_svc_channel_type_room_list_emit_listing_rooms (iface, FALSE);

  stop_listing (self);

  tp_svc_channel_type_room_list_return_from_stop_listing (context);
  */
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
  IdleRoomlistChannelPrivate *priv = self->priv;
  GValue room = {0,};
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

  keys = tp_asv_new (
      "name", G_TYPE_STRING, room_name,
      "members", G_TYPE_UINT, num_users,
      "subject", G_TYPE_STRING, topic,
      NULL);

  g_value_init (&room, TP_STRUCT_TYPE_ROOM_INFO);
  g_value_take_boxed (&room,
      dbus_g_type_specialized_construct (TP_STRUCT_TYPE_ROOM_INFO));

  dbus_g_type_struct_set (&room,
      0, room_handle,
      1, TP_IFACE_CHANNEL_TYPE_TEXT,
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
  IdleRoomlistChannelPrivate *priv = self->priv;

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
  IdleRoomlistChannelPrivate *priv = self->priv;
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
  IdleRoomlistChannelPrivate *priv = self->priv;

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


