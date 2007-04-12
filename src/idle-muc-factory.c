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

#include "idle-muc-factory.h"

#include "idle-connection.h"
#include "idle-parser.h"
#include "text.h"

#include <glib.h>

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>

static void _channel_factory_iface_init(gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(IdleMUCFactory, idle_muc_factory, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(TP_TYPE_CHANNEL_FACTORY_IFACE, _channel_factory_iface_init));

/* properties */
enum {
	PROP_CONNECTION = 1,
	LAST_PROPERTY_ENUM
};

typedef struct _IdleMUCFactoryPrivate IdleMUCFactoryPrivate;
struct _IdleMUCFactoryPrivate {
	IdleConnection *conn;
	GHashTable *channels;

	gboolean dispose_has_run;
};

#define IDLE_MUC_FACTORY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IDLE_TYPE_MUC_FACTORY, IdleMUCFactoryPrivate))

static IdleParserHandlerResult _numeric_error_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _join_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _namereply_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _notice_privmsg_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _part_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);

static void _iface_close_all(TpChannelFactoryIface *iface);
static void _iface_connecting(TpChannelFactoryIface *iface);
static void _iface_disconnected(TpChannelFactoryIface *iface);
static void _iface_foreach(TpChannelFactoryIface *iface, TpChannelFunc func, gpointer user_data);
static TpChannelFactoryRequestStatus _iface_request(TpChannelFactoryIface *iface, const gchar *chan_type, TpHandleType handle_type, guint handle, gpointer request, TpChannelIface **new_chan, GError **error);

static IdleMUCChannel *_create_channel(IdleMUCFactory *factory, TpHandle handle);
static void _channel_closed_cb(IdleMUCChannel *chan, gpointer user_data);
static void _channel_join_ready_cb(IdleMUCChannel *chan, guint err, gpointer user_data);

static void idle_muc_factory_init(IdleMUCFactory *obj) {
	IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(obj);

	priv->channels = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_object_unref);
}

static void idle_muc_factory_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
  IdleMUCFactory *fac = IDLE_MUC_FACTORY(object);
  IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(fac);

  switch(property_id) {
    case PROP_CONNECTION:
      g_value_set_object(value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void idle_muc_factory_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
  IdleMUCFactory *fac = IDLE_MUC_FACTORY(object);
  IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(fac);

  switch(property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void idle_muc_factory_class_init(IdleMUCFactoryClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GParamSpec *param_spec;

  g_type_class_add_private(klass, sizeof(IdleMUCFactoryPrivate));

  object_class->get_property = idle_muc_factory_get_property;
  object_class->set_property = idle_muc_factory_set_property;

  param_spec = g_param_spec_object("connection", "IdleConnection object", "The IdleConnection object that owns this IM channel factory object.", IDLE_TYPE_CONNECTION, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_CONNECTION, param_spec);
}

static IdleParserHandlerResult _numeric_error_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(user_data);
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	IdleMUCChannel *chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	if (!chan)
		return IDLE_PARSER_HANDLER_RESULT_HANDLED;

	switch (code) {
		case IDLE_PARSER_NUMERIC_BADCHANNELKEY:
			_idle_muc_channel_badchannelkey(chan);
			break;

		case IDLE_PARSER_NUMERIC_BANNEDFROMCHAN:
			_idle_muc_channel_join_error(chan, MUC_CHANNEL_JOIN_ERROR_BANNED);
			break;

		case IDLE_PARSER_NUMERIC_CHANNELISFULL:
			_idle_muc_channel_join_error(chan, MUC_CHANNEL_JOIN_ERROR_FULL);
			break;

		case IDLE_PARSER_NUMERIC_INVITEONLYCHAN:
			_idle_muc_channel_join_error(chan, MUC_CHANNEL_JOIN_ERROR_INVITE_ONLY);
			break;

		default:
			g_assert_not_reached();
			break;
	}

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _join_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCFactory *factory = IDLE_MUC_FACTORY(user_data);
	IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(factory);
	TpHandle joiner_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 1));
	IdleMUCChannel *chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	if (!chan)
		chan = _create_channel(factory, room_handle);

	_idle_muc_channel_join(chan, joiner_handle);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _namereply_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(user_data);
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	IdleMUCChannel *chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	if (chan)
		_idle_muc_channel_namereply(chan, args);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _notice_privmsg_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCFactory *factory = IDLE_MUC_FACTORY(user_data);
	IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(factory);
	TpHandle sender_handle = (TpHandle) g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandle room_handle = (TpHandle) g_value_get_uint(g_value_array_get_nth(args, 1));
	IdleMUCChannel *chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	TpChannelTextMessageType type;
	gchar *body;

	if (code == IDLE_PARSER_PREFIXCMD_NOTICE_CHANNEL) {
		type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE;
		body = g_value_dup_string(g_value_array_get_nth(args, 2));
	} else {
		idle_text_decode(g_value_get_string(g_value_array_get_nth(args, 2)), &type, &body);
	}

	if (type == -1)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	if (chan)
		_idle_muc_channel_receive(chan, type, sender_handle, body);

	g_free(body);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _part_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(user_data);
	TpHandle leaver_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 1));
	const gchar *message = (args->n_values == 3) ? g_value_get_string(g_value_array_get_nth(args, 2)) : NULL;
	IdleMUCChannel *chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	if (chan)
		_idle_muc_channel_part(chan, leaver_handle, message);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static void _iface_close_all(TpChannelFactoryIface *iface) {
	IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(iface);
	GHashTable *tmp;

	tmp = priv->channels;
	priv->channels = NULL;
	g_hash_table_destroy(tmp);
}

static void _iface_connecting(TpChannelFactoryIface *iface) {
	IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(iface);

	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_NUMERIC_BADCHANNELKEY, _numeric_error_handler, iface);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_NUMERIC_BANNEDFROMCHAN, _numeric_error_handler, iface);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_NUMERIC_CHANNELISFULL, _numeric_error_handler, iface);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_NUMERIC_INVITEONLYCHAN, _numeric_error_handler, iface);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_NUMERIC_NAMEREPLY, _namereply_handler, iface);

	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_JOIN, _join_handler, iface);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_NOTICE_CHANNEL, _notice_privmsg_handler, iface);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_PRIVMSG_CHANNEL, _notice_privmsg_handler, iface);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_PART, _part_handler, iface);
}

static void _iface_disconnected(TpChannelFactoryIface *iface) {
	IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(iface);

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
	IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(iface);
	struct _ForeachHelperData data = {func, user_data};

	g_hash_table_foreach(priv->channels, _foreach_helper, &data);
}

static TpChannelFactoryRequestStatus _iface_request(TpChannelFactoryIface *iface, const gchar *chan_type, TpHandleType handle_type, guint handle, gpointer request, TpChannelIface **new_chan, GError **error) {
	IdleMUCFactory *factory = IDLE_MUC_FACTORY(iface);
	IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(factory);

	if (!g_str_equal(chan_type, TP_IFACE_CHANNEL_TYPE_TEXT))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

  if (handle_type != TP_HANDLE_TYPE_ROOM)
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

  if (!tp_handle_is_valid(tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->conn), TP_HANDLE_TYPE_ROOM), handle, error))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;

	if ((*new_chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(handle)))) {
		return TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
	} else {
		_create_channel(factory, handle);
		return TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED;
	}
}

static IdleMUCChannel *_create_channel(IdleMUCFactory *factory, TpHandle handle) {
	IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(factory);
	IdleMUCChannel *chan;
	gchar *object_path;

	object_path = g_strdup_printf("%s/MucChannel%u", priv->conn->parent.object_path, handle);
	chan = g_object_new(IDLE_TYPE_MUC_CHANNEL, "connection", priv->conn, "object-path", object_path, "handle", handle, NULL);

	g_signal_connect(chan, "closed", (GCallback) _channel_closed_cb, factory);
	g_signal_connect(chan, "join-ready", (GCallback) _channel_join_ready_cb, factory);

	g_hash_table_insert(priv->channels, GUINT_TO_POINTER(handle), chan);

	_idle_muc_channel_join_attempt(chan);

	g_free(object_path);

	return chan;
}

static void _channel_closed_cb(IdleMUCChannel *chan, gpointer user_data) {
	IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(user_data);

	if (priv->channels) {
		TpHandle handle;
		g_object_get(chan, "handle", &handle, NULL);
		g_hash_table_remove(priv->channels, GUINT_TO_POINTER(handle));
	}
}

static void _channel_join_ready_cb(IdleMUCChannel *chan, guint err, gpointer user_data) {
	TpChannelFactoryIface *iface = TP_CHANNEL_FACTORY_IFACE(user_data);
	IdleMUCFactoryPrivate *priv = IDLE_MUC_FACTORY_GET_PRIVATE(user_data);

	if (err == MUC_CHANNEL_JOIN_ERROR_NONE) {
		tp_channel_factory_iface_emit_new_channel(iface, (TpChannelIface *) chan, NULL);
		return;
	}

	GError error = {TP_ERRORS, 0, NULL};
	TpHandle handle;

	g_object_get(chan, "handle", &handle, NULL);

	switch (err) {
		case MUC_CHANNEL_JOIN_ERROR_BANNED:
			error.code = TP_ERROR_CHANNEL_BANNED;
			error.message = "You are banned from the channel.";
			break;

		case MUC_CHANNEL_JOIN_ERROR_FULL:
			error.code = TP_ERROR_CHANNEL_FULL;
			error.message = "The channel is full.";
			break;

		case MUC_CHANNEL_JOIN_ERROR_INVITE_ONLY:
			error.code = TP_ERROR_CHANNEL_INVITE_ONLY;
			error.message = "The channel is invite only.";
			break;

		default:
			g_assert_not_reached();
			break;
	}

	tp_channel_factory_iface_emit_channel_error(iface, (TpChannelIface *) chan, &error, NULL);

	g_hash_table_remove(priv->channels, GUINT_TO_POINTER(handle));
}

static void _channel_factory_iface_init(gpointer iface, gpointer data) {
	TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) iface;

	klass->close_all = _iface_close_all;
	klass->connected = NULL;
	klass->connecting = _iface_connecting;
	klass->disconnected = _iface_disconnected;
	klass->foreach = _iface_foreach;
	klass->request = _iface_request;
}

