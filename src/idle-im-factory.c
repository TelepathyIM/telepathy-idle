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

#include "idle-im-factory.h"

#include "idle-connection.h"
#include "idle-ctcp.h"
#include "idle-parser.h"
#include "idle-text.h"

#include <glib.h>

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>

static void _im_factory_iface_init(gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(IdleIMFactory, idle_im_factory, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(TP_TYPE_CHANNEL_FACTORY_IFACE, _im_factory_iface_init));

/* properties */
enum {
	PROP_CONNECTION = 1,
	LAST_PROPERTY_ENUM
};

typedef struct _IdleIMFactoryPrivate IdleIMFactoryPrivate;
struct _IdleIMFactoryPrivate {
	IdleConnection *conn;
	GHashTable *channels;

	gboolean dispose_has_run;
};

#define IDLE_IM_FACTORY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IDLE_TYPE_IM_FACTORY, IdleIMFactoryPrivate))

static IdleParserHandlerResult _nick_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _notice_privmsg_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);

static void _iface_close_all(TpChannelFactoryIface *iface);
static void _iface_connecting(TpChannelFactoryIface *iface);
static void _iface_disconnected(TpChannelFactoryIface *iface);
static void _iface_foreach(TpChannelFactoryIface *iface, TpChannelFunc func, gpointer user_data);
static TpChannelFactoryRequestStatus _iface_request(TpChannelFactoryIface *iface, const gchar *chan_type, TpHandleType handle_type, guint handle, gpointer request, TpChannelIface **new_chan, GError **error);

static IdleIMChannel *_create_channel(IdleIMFactory *factory, TpHandle handle, gpointer context);
static void _channel_closed_cb(IdleIMChannel *chan, gpointer user_data);

static void idle_im_factory_init(IdleIMFactory *obj) {
	IdleIMFactoryPrivate *priv = IDLE_IM_FACTORY_GET_PRIVATE(obj);

	priv->channels = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_object_unref);
}

static void idle_im_factory_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
  IdleIMFactory *fac = IDLE_IM_FACTORY(object);
  IdleIMFactoryPrivate *priv = IDLE_IM_FACTORY_GET_PRIVATE(fac);

  switch(property_id) {
    case PROP_CONNECTION:
      g_value_set_object(value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void idle_im_factory_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
  IdleIMFactory *fac = IDLE_IM_FACTORY(object);
  IdleIMFactoryPrivate *priv = IDLE_IM_FACTORY_GET_PRIVATE(fac);

  switch(property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void idle_im_factory_class_init(IdleIMFactoryClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GParamSpec *param_spec;

  g_type_class_add_private(klass, sizeof(IdleIMFactoryPrivate));

  object_class->get_property = idle_im_factory_get_property;
  object_class->set_property = idle_im_factory_set_property;

  param_spec = g_param_spec_object("connection", "IdleConnection object", "The IdleConnection object that owns this IM channel factory object.", IDLE_TYPE_CONNECTION, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_CONNECTION, param_spec);
}

static IdleParserHandlerResult _notice_privmsg_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleIMFactory *factory = IDLE_IM_FACTORY(user_data);
	IdleIMFactoryPrivate *priv = IDLE_IM_FACTORY_GET_PRIVATE(factory);
	TpHandle handle = (TpHandle) g_value_get_uint(g_value_array_get_nth(args, 0));
	IdleIMChannel *chan;
	TpChannelTextMessageType type;
	gchar *body;

	if (code == IDLE_PARSER_PREFIXCMD_NOTICE_USER) {
		type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE;
		body = idle_ctcp_kill_blingbling(g_value_get_string(g_value_array_get_nth(args, 2)));
	} else {
		idle_text_decode(g_value_get_string(g_value_array_get_nth(args, 2)), &type, &body);
	}

	if (type == -1)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	idle_connection_emit_queued_aliases_changed(priv->conn);

	if (!(chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(handle))))
		chan = _create_channel(factory, handle, NULL);

	_idle_im_channel_receive(chan, type, handle, body);

	g_free(body);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static void _iface_close_all(TpChannelFactoryIface *iface) {
	IdleIMFactoryPrivate *priv = IDLE_IM_FACTORY_GET_PRIVATE(iface);
	GHashTable *tmp;

	tmp = priv->channels;
	priv->channels = NULL;
	g_hash_table_destroy(tmp);
}

static void _iface_connecting(TpChannelFactoryIface *iface) {
	IdleIMFactoryPrivate *priv = IDLE_IM_FACTORY_GET_PRIVATE(iface);

	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_NOTICE_USER, _notice_privmsg_handler, iface);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_PRIVMSG_USER, _notice_privmsg_handler, iface);
}

static void _iface_disconnected(TpChannelFactoryIface *iface) {
	IdleIMFactoryPrivate *priv = IDLE_IM_FACTORY_GET_PRIVATE(iface);

	idle_parser_remove_handlers_by_data(priv->conn->parser, iface);
}

struct _ForeachHelperData {
	TpChannelFunc func;
	gpointer user_data;
};

static void _foreach_helper(gpointer key, gpointer value, gpointer user_data) {
	struct _ForeachHelperData *data = user_data;
	data->func(value, data->user_data);
}

static void _iface_foreach(TpChannelFactoryIface *iface, TpChannelFunc func, gpointer user_data) {
	IdleIMFactoryPrivate *priv = IDLE_IM_FACTORY_GET_PRIVATE(iface);
	struct _ForeachHelperData data = {func, user_data};

	g_hash_table_foreach(priv->channels, _foreach_helper, &data);
}

static TpChannelFactoryRequestStatus _iface_request(TpChannelFactoryIface *iface, const gchar *chan_type, TpHandleType handle_type, guint handle, gpointer request, TpChannelIface **new_chan, GError **error) {
	IdleIMFactory *factory = IDLE_IM_FACTORY(iface);
	IdleIMFactoryPrivate *priv = IDLE_IM_FACTORY_GET_PRIVATE(factory);

	if (!g_str_equal(chan_type, TP_IFACE_CHANNEL_TYPE_TEXT))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

  if (handle_type != TP_HANDLE_TYPE_CONTACT)
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

  if (!tp_handle_is_valid(tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->conn), TP_HANDLE_TYPE_CONTACT), handle, error))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR;

	if ((*new_chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(handle)))) {
		return TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
	} else {
		*new_chan = (TpChannelIface *) _create_channel(factory, handle, request);
		return TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
	}
}

static IdleIMChannel *_create_channel(IdleIMFactory *factory, TpHandle handle, gpointer context) {
	IdleIMFactoryPrivate *priv = IDLE_IM_FACTORY_GET_PRIVATE(factory);
	IdleIMChannel *chan;
	gchar *object_path;

	object_path = g_strdup_printf("%s/ImChannel%u", priv->conn->parent.object_path, handle);
	chan = g_object_new(IDLE_TYPE_IM_CHANNEL, "connection", priv->conn, "object-path", object_path, "handle", handle, NULL);

	g_signal_connect(chan, "closed", (GCallback) _channel_closed_cb, factory);
	g_hash_table_insert(priv->channels, GUINT_TO_POINTER(handle), chan);
	tp_channel_factory_iface_emit_new_channel(factory, (TpChannelIface *) chan, context);

	g_free(object_path);

	return chan;
}

static void _channel_closed_cb(IdleIMChannel *chan, gpointer user_data) {
	IdleIMFactoryPrivate *priv = IDLE_IM_FACTORY_GET_PRIVATE(user_data);
	TpHandle handle;

	if (priv->channels) {
		g_object_get(chan, "handle", &handle, NULL);
		g_hash_table_remove(priv->channels, GUINT_TO_POINTER(handle));
	}
}

static void _im_factory_iface_init(gpointer iface, gpointer data) {
	TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) iface;

	klass->close_all = _iface_close_all;
	klass->connected = NULL;
	klass->connecting = _iface_connecting;
	klass->disconnected = _iface_disconnected;
	klass->foreach = _iface_foreach;
	klass->request = _iface_request;
}

