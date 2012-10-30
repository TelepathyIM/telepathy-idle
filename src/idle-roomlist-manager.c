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

#include "idle-roomlist-manager.h"

#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/util.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_ROOMLIST
#include "idle-connection.h"
#include "idle-debug.h"
#include "idle-roomlist-channel.h"
#include "idle-parser.h"

static void _roomlist_manager_iface_init (gpointer g_iface, gpointer iface_data);
static GObject * _roomlist_manager_constructor (GType type, guint n_props, GObjectConstructParam *props);
static void _roomlist_manager_dispose (GObject *object);

G_DEFINE_TYPE_WITH_CODE(IdleRoomlistManager, idle_roomlist_manager, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(TP_TYPE_CHANNEL_MANAGER, _roomlist_manager_iface_init));

/* properties */
enum {
    PROP_CONNECTION = 1,
    LAST_PROPERTY_ENUM
};

static const gchar * const roomlist_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const roomlist_channel_allowed_properties[] = {
    NULL
};

struct _IdleRoomlistManagerPrivate
{
  IdleConnection *conn;
  IdleRoomlistChannel *channel;
  int status_changed_id;
  gboolean dispose_has_run;
};

static void _roomlist_manager_close_all (IdleRoomlistManager *self);
static void connection_status_changed_cb (IdleConnection* conn, guint status, guint reason, IdleRoomlistManager *self);

static void _roomlist_manager_foreach (TpChannelManager *self, TpExportableChannelFunc func, gpointer user_data);
static void _roomlist_manager_foreach_class (TpChannelManager *self, TpChannelManagerChannelClassFunc func, gpointer user_data);

static gboolean _roomlist_manager_create_channel (TpChannelManager *self, gpointer request_token, GHashTable *request_properties);
static gboolean _roomlist_manager_request_channel (TpChannelManager *self, gpointer request_token, GHashTable *request_properties);
static gboolean _roomlist_manager_ensure_channel (TpChannelManager *self, gpointer request_token, GHashTable *request_properties);
static gboolean _roomlist_manager_requestotron (IdleRoomlistManager *self, gpointer request_token, GHashTable *request_properties, gboolean require_new);
static IdleRoomlistChannel *_roomlist_manager_new_channel (IdleRoomlistManager *self, gpointer request);

static void _roomlist_channel_closed_cb (IdleRoomlistChannel *chan, gpointer user_data);

static void
idle_roomlist_manager_init (IdleRoomlistManager *self)
{
    IdleRoomlistManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        IDLE_TYPE_ROOMLIST_MANAGER, IdleRoomlistManagerPrivate);

    self->priv = priv;
    priv->channel = NULL;
    priv->status_changed_id = 0;
    priv->dispose_has_run = FALSE;
}


static void
idle_roomlist_manager_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
    IdleRoomlistManager *self = IDLE_ROOMLIST_MANAGER (object);
    IdleRoomlistManagerPrivate *priv = self->priv;

    switch (property_id) {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}


static void
idle_roomlist_manager_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
    IdleRoomlistManager *self = IDLE_ROOMLIST_MANAGER (object);
    IdleRoomlistManagerPrivate *priv = self->priv;

    switch (property_id) {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}


static void
idle_roomlist_manager_class_init (IdleRoomlistManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamSpec *param_spec;

    g_type_class_add_private (klass, sizeof (IdleRoomlistManagerPrivate));

    object_class->constructor = _roomlist_manager_constructor;
    object_class->dispose = _roomlist_manager_dispose;
    object_class->get_property = idle_roomlist_manager_get_property;
    object_class->set_property = idle_roomlist_manager_set_property;

    param_spec = g_param_spec_object ("connection", "IdleConnection object",
        "The IdleConnection object that owns this Roomlist channel manager object.",
        IDLE_TYPE_CONNECTION, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
        G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}


static GObject *
_roomlist_manager_constructor (GType type,
                               guint n_props,
                               GObjectConstructParam *props)
{
  GObject *obj;
  IdleRoomlistManager *self;
  IdleRoomlistManagerPrivate *priv;

  obj = G_OBJECT_CLASS (idle_roomlist_manager_parent_class)->constructor
    (type, n_props, props);

  self = IDLE_ROOMLIST_MANAGER (obj);
  priv = self->priv;

  g_return_val_if_fail (priv->conn, obj);

  priv->status_changed_id = g_signal_connect (priv->conn,
      "status-changed", (GCallback)
      connection_status_changed_cb,
      self);

  return obj;
}


static void
_roomlist_manager_close_all (IdleRoomlistManager *self)
{
    IdleRoomlistManagerPrivate *priv = self->priv;
    IdleRoomlistChannel *tmp;

    if (priv->channel != NULL)
      {
        /* use a temporary variable and set priv->channel to NULL first
         * because unreffing this channel will cause the
         * _roomlist_channel_closed_cb to fire, which will try to unref it
         * again if priv->channel is not NULL */
        tmp = priv->channel;
        priv->channel = NULL;
        g_object_unref(tmp);
      }
    if (priv->status_changed_id != 0)
      {
        g_signal_handler_disconnect (priv->conn, priv->status_changed_id);
        priv->status_changed_id = 0;
      }
}


static void
connection_status_changed_cb (IdleConnection* conn,
                              guint status,
                              guint reason,
                              IdleRoomlistManager *self)
{
  if (status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      _roomlist_manager_close_all (self);
    }
}


static void
_roomlist_manager_foreach (TpChannelManager *manager,
                           TpExportableChannelFunc func,
                           gpointer user_data)
{
    IdleRoomlistManager *self = IDLE_ROOMLIST_MANAGER (manager);
    IdleRoomlistManagerPrivate *priv = self->priv;

    if (!priv->channel)
      {
        IDLE_DEBUG ("Channel missing, ignoring...");
        return;
      }

    func (TP_EXPORTABLE_CHANNEL (priv->channel), user_data);
}


static void
_roomlist_manager_foreach_class (TpChannelManager *self,
                                 TpChannelManagerChannelClassFunc func,
                                 gpointer user_data)
{
  GHashTable *table = tp_asv_new (
      roomlist_channel_fixed_properties[0], G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_ROOM_LIST,
      roomlist_channel_fixed_properties[1], G_TYPE_UINT,
          TP_HANDLE_TYPE_NONE,
      NULL);

  func (self, table, roomlist_channel_allowed_properties, user_data);

  g_hash_table_destroy (table);
}


static gboolean
_roomlist_manager_create_channel (TpChannelManager *self,
                                  gpointer request_token,
                                  GHashTable *request_properties)
{
  IdleRoomlistManager *mgr = IDLE_ROOMLIST_MANAGER (self);

  return _roomlist_manager_requestotron (mgr, request_token, request_properties,
      TRUE);
}


static gboolean
_roomlist_manager_request_channel (TpChannelManager *self,
                                   gpointer request_token,
                                   GHashTable *request_properties)
{
  IdleRoomlistManager *mgr = IDLE_ROOMLIST_MANAGER (self);

  return _roomlist_manager_requestotron (mgr, request_token,
      request_properties, FALSE);
}


static gboolean
_roomlist_manager_ensure_channel (TpChannelManager *self,
                                  gpointer request_token,
                                  GHashTable *request_properties)
{
  IdleRoomlistManager *mgr = IDLE_ROOMLIST_MANAGER (self);

  return _roomlist_manager_requestotron (mgr, request_token,
      request_properties, FALSE);
}


static gboolean
_roomlist_manager_requestotron (IdleRoomlistManager *self,
                                gpointer request_token,
                                GHashTable *request_properties,
                                gboolean require_new)
{
  IdleRoomlistManagerPrivate *priv = self->priv;
  GError *error = NULL;

  IDLE_DEBUG("requesting new room list channel");

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"), TP_IFACE_CHANNEL_TYPE_ROOM_LIST))
    return FALSE;

  if (tp_asv_get_uint32 (request_properties,
        TP_IFACE_CHANNEL ".TargetHandleType", NULL) != 0)
    return FALSE;

  /* Check if there are any other properties that we don't understand */
  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
        roomlist_channel_fixed_properties,
        roomlist_channel_allowed_properties,
        &error))
    {
      goto error;
    }

  if (priv->channel == NULL)
    {
      _roomlist_manager_new_channel (self, request_token);
      return TRUE;
    }

  if (require_new)
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Only one room list channel can be created");
      goto error;
    }

  tp_channel_manager_emit_request_already_satisfied (self, request_token,
      TP_EXPORTABLE_CHANNEL (priv->channel));
  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}


static void
_roomlist_channel_closed_cb (IdleRoomlistChannel *chan,
                             gpointer user_data)
{
  IdleRoomlistManager *self = IDLE_ROOMLIST_MANAGER (user_data);
  IdleRoomlistManagerPrivate *priv = self->priv;

  tp_channel_manager_emit_channel_closed_for_object (self,
      TP_EXPORTABLE_CHANNEL (chan));

  if (priv->channel)
    {
      g_assert (priv->channel == chan);
      g_object_unref (priv->channel);
      priv->channel = NULL;
    }
}


static IdleRoomlistChannel *
_roomlist_manager_new_channel (IdleRoomlistManager *self,
                               gpointer request)
{
  IdleRoomlistManagerPrivate *priv = self->priv;
  IdleRoomlistChannel *chan;
  GSList *requests = NULL;

  g_assert (priv->channel == NULL);

  IDLE_DEBUG ("Requested room list channel");

  chan = g_object_new (IDLE_TYPE_ROOMLIST_CHANNEL,
                       "connection", priv->conn,
                       NULL);

  if (request != NULL)
    requests = g_slist_prepend (requests, request);

  tp_channel_manager_emit_new_channel (self, TP_EXPORTABLE_CHANNEL (chan),
      requests);

  g_slist_free (requests);

  g_signal_connect (chan, "closed", G_CALLBACK (_roomlist_channel_closed_cb), self);
  priv->channel = chan;

  return chan;
}


static void
_roomlist_manager_iface_init (gpointer g_iface,
                              gpointer iface_data)
{
    TpChannelManagerIface *iface = g_iface;

    iface->foreach_channel = _roomlist_manager_foreach;
    iface->foreach_channel_class = _roomlist_manager_foreach_class;
    iface->request_channel = _roomlist_manager_request_channel;
    iface->create_channel = _roomlist_manager_create_channel;
    iface->ensure_channel = _roomlist_manager_ensure_channel;
}


static void
_roomlist_manager_dispose (GObject *object)
{
  IdleRoomlistManager *self = IDLE_ROOMLIST_MANAGER (object);
  IdleRoomlistManagerPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  _roomlist_manager_close_all (self);

  if (G_OBJECT_CLASS (idle_roomlist_manager_parent_class)->dispose)
    G_OBJECT_CLASS (idle_roomlist_manager_parent_class)->dispose (object);
}

