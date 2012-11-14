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

#include "config.h"

#include "idle-muc-manager.h"

#include <time.h>

#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_MUC
#include "idle-connection.h"
#include "idle-ctcp.h"
#include "idle-debug.h"
#include "idle-muc-channel.h"
#include "idle-parser.h"
#include "idle-text.h"

static void _muc_manager_iface_init(gpointer, gpointer);
static GObject* _muc_manager_constructor(GType type, guint n_props, GObjectConstructParam *props);

G_DEFINE_TYPE_WITH_CODE(IdleMUCManager, idle_muc_manager, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(TP_TYPE_CHANNEL_MANAGER, _muc_manager_iface_init));

/* properties */
enum {
	PROP_CONNECTION = 1,
	LAST_PROPERTY_ENUM
};

typedef struct _IdleMUCManagerPrivate IdleMUCManagerPrivate;
struct _IdleMUCManagerPrivate {
	IdleConnection *conn;
	GHashTable *channels;

	/* Map from IdleMUCChannel * (borrowed from channels) to a GSList * of
	 * request tokens. */
	GHashTable *queued_requests;

	gulong status_changed_id;
	gboolean dispose_has_run;
};

#define IDLE_MUC_MANAGER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IDLE_TYPE_MUC_MANAGER, IdleMUCManagerPrivate))

static IdleParserHandlerResult _numeric_error_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _numeric_namereply_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _numeric_namereply_end_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _numeric_topic_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _numeric_topic_stamp_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);

static IdleParserHandlerResult _invite_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _join_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _kick_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _mode_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _nick_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _notice_privmsg_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _part_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _quit_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _topic_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);

static void connection_status_changed_cb (IdleConnection *conn, guint status, guint reason, IdleMUCManager *self);
static void _muc_manager_close_all(IdleMUCManager *manager);
static void _muc_manager_add_handlers(IdleMUCManager *manager);

static IdleMUCChannel *_muc_manager_new_channel(IdleMUCManager *manager, TpHandle handle, TpHandle initiator, gboolean requested);

static void _channel_closed_cb(IdleMUCChannel *chan, gpointer user_data);
static void _channel_join_ready_cb(IdleMUCChannel *chan, guint err, gpointer user_data);


static const gchar * const muc_channel_fixed_properties[] = {
    TP_PROP_CHANNEL_CHANNEL_TYPE,
    TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
    NULL
};

static const gchar * const muc_channel_allowed_properties[] = {
    TP_PROP_CHANNEL_TARGET_HANDLE,
    TP_PROP_CHANNEL_TARGET_ID,
    NULL
};

static const gchar * const muc_channel_allowed_room_properties[] = {
    TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, /* But it must be None */
    TP_PROP_CHANNEL_INTERFACE_ROOM_ROOM_NAME,
    NULL
};

static const gchar * const muc_channel_all_allowed_properties[] = {
    TP_PROP_CHANNEL_TARGET_HANDLE,
    TP_PROP_CHANNEL_TARGET_ID,
    TP_PROP_CHANNEL_INTERFACE_ROOM_ROOM_NAME,
};

static GObject*
_muc_manager_constructor(GType type, guint n_props, GObjectConstructParam *props)
{
	GObject *obj;
	IdleMUCManagerPrivate *priv;

	obj = G_OBJECT_CLASS (idle_muc_manager_parent_class)-> constructor (type,
																		n_props,
																		props);

	priv = IDLE_MUC_MANAGER_GET_PRIVATE (obj);

	priv->status_changed_id =
		g_signal_connect (priv->conn, "status-changed",
						  (GCallback) connection_status_changed_cb, obj);

	return obj;
}

static void idle_muc_manager_init(IdleMUCManager *obj) {
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(obj);

	priv->channels = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_object_unref);
	priv->queued_requests = g_hash_table_new(NULL, NULL);
}

static void idle_muc_manager_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
	IdleMUCManager *fac = IDLE_MUC_MANAGER(object);
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(fac);

	switch (property_id) {
		case PROP_CONNECTION:
			g_value_set_object(value, priv->conn);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void idle_muc_manager_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
	IdleMUCManager *fac = IDLE_MUC_MANAGER(object);
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(fac);

	switch (property_id) {
		case PROP_CONNECTION:
			priv->conn = g_value_get_object(value);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void idle_muc_manager_class_init(IdleMUCManagerClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *param_spec;

	g_type_class_add_private(klass, sizeof(IdleMUCManagerPrivate));

	object_class->constructor = _muc_manager_constructor;
	object_class->get_property = idle_muc_manager_get_property;
	object_class->set_property = idle_muc_manager_set_property;

	param_spec = g_param_spec_object("connection", "IdleConnection object", "The IdleConnection object that owns this IM channel manager object.", IDLE_TYPE_CONNECTION, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
	g_object_class_install_property(object_class, PROP_CONNECTION, param_spec);
}

static IdleParserHandlerResult _numeric_error_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(user_data);
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	IdleMUCChannel *chan;

	if (!priv->channels) {
		IDLE_DEBUG("Channels hash table missing, ignoring...");
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	if (!chan)
		return IDLE_PARSER_HANDLER_RESULT_HANDLED;

	switch (code) {
		case IDLE_PARSER_NUMERIC_BADCHANNELKEY:
			idle_muc_channel_badchannelkey(chan);
			break;

		case IDLE_PARSER_NUMERIC_BANNEDFROMCHAN:
			idle_muc_channel_join_error(chan, MUC_CHANNEL_JOIN_ERROR_BANNED);
			break;

		case IDLE_PARSER_NUMERIC_CHANNELISFULL:
			idle_muc_channel_join_error(chan, MUC_CHANNEL_JOIN_ERROR_FULL);
			break;

		case IDLE_PARSER_NUMERIC_INVITEONLYCHAN:
			idle_muc_channel_join_error(chan, MUC_CHANNEL_JOIN_ERROR_INVITE_ONLY);
			break;

		default:
			g_assert_not_reached();
			break;
	}

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _numeric_topic_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(user_data);
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	const gchar *topic = g_value_get_string(g_value_array_get_nth(args, 1));
	IdleMUCChannel *chan;

	if (!priv->channels) {
		IDLE_DEBUG("Channels hash table missing, ignoring...");
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	if (chan)
		idle_muc_channel_topic(chan, topic);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _numeric_topic_stamp_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(user_data);
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandle toucher_handle = g_value_get_uint(g_value_array_get_nth(args, 1));
	time_t touched = g_value_get_uint(g_value_array_get_nth(args, 2));
	IdleMUCChannel *chan;

	if (!priv->channels) {
		IDLE_DEBUG("Channels hash table missing, ignoring...");
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	idle_connection_emit_queued_aliases_changed(priv->conn);

	if (chan)
		idle_muc_channel_topic_touch(chan, toucher_handle, touched);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _invite_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCManager *manager = IDLE_MUC_MANAGER(user_data);
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(manager);
	TpHandle inviter_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandle invited_handle = g_value_get_uint(g_value_array_get_nth(args, 1));
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 2));
	IdleMUCChannel *chan;

	if (invited_handle != priv->conn->parent.self_handle)
		return IDLE_PARSER_HANDLER_RESULT_HANDLED;

	if (!priv->channels) {
		IDLE_DEBUG("Channels hash table missing, ignoring...");
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	idle_connection_emit_queued_aliases_changed(priv->conn);

	if (!chan) {
		chan = _muc_manager_new_channel(manager, room_handle, inviter_handle, FALSE);
		tp_channel_manager_emit_new_channel(TP_CHANNEL_MANAGER(user_data), (TpExportableChannel *) chan, NULL);
		idle_muc_channel_invited(chan, inviter_handle);
	}

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _join_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCManager *manager = IDLE_MUC_MANAGER(user_data);
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(manager);
	TpHandle joiner_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 1));
	IdleMUCChannel *chan;

	idle_connection_emit_queued_aliases_changed(priv->conn);

	if (!priv->channels) {
		IDLE_DEBUG("Channels hash table missing, ignoring...");
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	if (!chan) {
		/* TODO: If we're in "bouncer mode", maybe these should be Requested:
		 * True? At least for the initial batch? */
		chan = _muc_manager_new_channel(manager, room_handle, 0, FALSE);
	}

	idle_muc_channel_join(chan, joiner_handle);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _kick_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(user_data);
	TpHandle kicker_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 1));
	TpHandle kicked_handle = g_value_get_uint(g_value_array_get_nth(args, 2));
	const gchar *message = (args->n_values == 4) ? g_value_get_string(g_value_array_get_nth(args, 3)) : NULL;
	IdleMUCChannel *chan;

	if (!priv->channels) {
		IDLE_DEBUG("Channels hash table missing, ignoring...");
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	if (chan)
		idle_muc_channel_kick(chan, kicked_handle, kicker_handle, message);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _numeric_namereply_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(user_data);
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	IdleMUCChannel *chan;

	if (!priv->channels) {
		IDLE_DEBUG("Channels hash table missing, ignoring...");
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	if (chan)
		idle_muc_channel_namereply(chan, args);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _numeric_namereply_end_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(user_data);
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	IdleMUCChannel *chan;

	if (!priv->channels) {
		IDLE_DEBUG("Channels hash table missing, ignoring...");
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	if (chan)
		idle_muc_channel_namereply_end(chan);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _mode_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(user_data);
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	IdleMUCChannel *chan;

	if (!priv->channels) {
		IDLE_DEBUG("Channels hash table missing, ignoring...");
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	if (chan)
		idle_muc_channel_mode(chan, args);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

typedef struct _ChannelRenameForeachData ChannelRenameForeachData;
struct _ChannelRenameForeachData {
	TpHandle old_handle, new_handle;
};

static void _channel_rename_foreach(TpExportableChannel *channel, gpointer user_data) {
	IdleMUCChannel *muc_chan = IDLE_MUC_CHANNEL(channel);
	ChannelRenameForeachData *data = user_data;

	idle_muc_channel_rename(muc_chan, data->old_handle, data->new_handle);
}

static IdleParserHandlerResult _nick_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	TpChannelManager *mgr = TP_CHANNEL_MANAGER(user_data);
	TpHandle old_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandle new_handle = g_value_get_uint(g_value_array_get_nth(args, 1));
	ChannelRenameForeachData data = {old_handle, new_handle};

	if (old_handle == new_handle)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	tp_channel_manager_foreach_channel(mgr, _channel_rename_foreach, &data);

	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _notice_privmsg_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCManager *manager = IDLE_MUC_MANAGER(user_data);
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(manager);
	TpHandle sender_handle = (TpHandle) g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandle room_handle = (TpHandle) g_value_get_uint(g_value_array_get_nth(args, 1));
	IdleMUCChannel *chan;
	TpChannelTextMessageType type;
	gchar *body;

	if (!priv->channels) {
		IDLE_DEBUG("Channels hash table missing, ignoring...");
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));
	/* XXX: just check for chan == NULL here and bail with NOT_HANDLED if room
	 * was not found ?  Currently we go through all of the decoding of the
	 * message, but don't actually deliver the message to a channel if chan is
	 * NULL, and then we return 'HANDLED', which seems wrong
	 */

	if (code == IDLE_PARSER_PREFIXCMD_NOTICE_CHANNEL) {
		type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE;
		body = idle_ctcp_kill_blingbling(g_value_get_string(g_value_array_get_nth(args, 2)));
	} else {
		gboolean decoded = idle_text_decode(g_value_get_string(g_value_array_get_nth(args, 2)), &type, &body);
		if (!decoded)
			return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	if (chan)
		idle_muc_channel_receive(chan, type, sender_handle, body);

	g_free(body);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}


static IdleParserHandlerResult _part_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(user_data);
	TpHandle leaver_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 1));
	const gchar *message = (args->n_values == 3) ? g_value_get_string(g_value_array_get_nth(args, 2)) : NULL;
	IdleMUCChannel *chan;

	if (!priv->channels) {
		IDLE_DEBUG("Channels hash table missing, ignoring...");
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	if (chan)
		idle_muc_channel_part(chan, leaver_handle, message);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

typedef struct _ChannelQuitForeachData ChannelQuitForeachData;
struct _ChannelQuitForeachData {
	TpHandle handle;
	const gchar *message;
};

static void _channel_quit_foreach(TpExportableChannel *channel, gpointer user_data) {
	IdleMUCChannel *muc_chan = IDLE_MUC_CHANNEL(channel);
	ChannelQuitForeachData *data = user_data;

	idle_muc_channel_quit(muc_chan, data->handle, data->message);
}

static IdleParserHandlerResult _quit_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	TpChannelManager *manager = TP_CHANNEL_MANAGER(user_data);
	TpHandle leaver_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	const gchar *message = (args->n_values == 2) ? g_value_get_string(g_value_array_get_nth(args, 1)) : NULL;
	ChannelQuitForeachData data = {leaver_handle, message};

	tp_channel_manager_foreach_channel(manager, _channel_quit_foreach, &data);

	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _topic_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(user_data);
	TpHandle setter_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandle room_handle = g_value_get_uint(g_value_array_get_nth(args, 1));
	const gchar *topic = (args->n_values == 3) ? g_value_get_string(g_value_array_get_nth(args, 2)) : NULL;
	time_t stamp = time(NULL);
	IdleMUCChannel *chan;

	if (!priv->channels) {
		IDLE_DEBUG("Channels hash table missing, ignoring...");
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(room_handle));

	if (chan) {
		if (topic)
			idle_muc_channel_topic_full(chan, setter_handle, stamp, topic);
		else
			idle_muc_channel_topic_unset(chan);
	}

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static void _muc_manager_close_all(IdleMUCManager *manager)
{
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(manager);

	if (priv->status_changed_id != 0) {
		g_signal_handler_disconnect (priv->conn, priv->status_changed_id);
		priv->status_changed_id = 0;
	}

	if (!priv->channels) {
		IDLE_DEBUG("Channels already closed, ignoring...");
		return;
	}

	tp_clear_pointer (&priv->channels, g_hash_table_destroy);
}

static void
connection_status_changed_cb (IdleConnection *conn,
							  guint status,
							  guint reason,
							  IdleMUCManager *self)
{
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(self);
	switch (status)
	{
		case TP_CONNECTION_STATUS_CONNECTED:
			_muc_manager_add_handlers(self);
			break;
		case TP_CONNECTION_STATUS_DISCONNECTED:
			idle_parser_remove_handlers_by_data(priv->conn->parser, self);
			_muc_manager_close_all(self);
			break;
	}
}


static void _muc_manager_add_handlers(IdleMUCManager *manager)
{
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(manager);

	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_NUMERIC_BADCHANNELKEY, _numeric_error_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_NUMERIC_BANNEDFROMCHAN, _numeric_error_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_NUMERIC_CHANNELISFULL, _numeric_error_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_NUMERIC_INVITEONLYCHAN, _numeric_error_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_NUMERIC_MODEREPLY, _mode_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_NUMERIC_NAMEREPLY, _numeric_namereply_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_NUMERIC_NAMEREPLY_END, _numeric_namereply_end_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_NUMERIC_TOPIC, _numeric_topic_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_NUMERIC_TOPIC_STAMP, _numeric_topic_stamp_handler, manager);

	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_INVITE, _invite_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_JOIN, _join_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_KICK, _kick_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_MODE_CHANNEL, _mode_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_NICK, _nick_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_NOTICE_CHANNEL, _notice_privmsg_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_PRIVMSG_CHANNEL, _notice_privmsg_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_PART, _part_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_QUIT, _quit_handler, manager);
	idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_TOPIC, _topic_handler, manager);
}

static void
_muc_manager_foreach_channel (
    TpChannelManager *iface,
    TpExportableChannelFunc func,
    gpointer user_data)
{
  IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE (iface);

  if (priv->channels == NULL)
    {
      IDLE_DEBUG ("Channels hash table missing, ignoring...");
    }
  else
    {
      GHashTableIter iter;
      gpointer v;

      g_hash_table_iter_init (&iter, priv->channels);
      while (g_hash_table_iter_next (&iter, NULL, &v))
        func (TP_EXPORTABLE_CHANNEL (v), user_data);
    }
}

static void
_muc_manager_type_foreach_channel_class (
    GType type,
    TpChannelManagerTypeChannelClassFunc func,
    gpointer user_data)
{
  static GHashTable *handle_fixed = NULL, *room_name_fixed = NULL;

  if (G_UNLIKELY (handle_fixed == NULL))
    {
      handle_fixed = tp_asv_new (
          TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_TEXT,
          TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_ROOM,
          NULL);
      room_name_fixed = tp_asv_new (
          TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_TEXT,
          NULL);
    }

  func (type, handle_fixed, muc_channel_allowed_properties, user_data);
  func (type, room_name_fixed, muc_channel_allowed_room_properties, user_data);
}


static IdleMUCChannel *_muc_manager_new_channel(IdleMUCManager *manager, TpHandle handle, TpHandle initiator, gboolean requested)
{
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(manager);
	IdleMUCChannel *chan;

	g_assert(g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(handle)) == NULL);

	chan = idle_muc_channel_new(priv->conn, handle, initiator, requested);

	g_signal_connect(chan, "closed", (GCallback) _channel_closed_cb, manager);
	g_signal_connect(chan, "join-ready", (GCallback) _channel_join_ready_cb, manager);

	g_hash_table_insert(priv->channels, GUINT_TO_POINTER(handle), chan);

	return chan;
}

static void associate_request(IdleMUCManager *manager, IdleMUCChannel *chan, gpointer request) {
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(manager);
	GSList *reqs = g_hash_table_lookup(priv->queued_requests, chan);

	g_hash_table_steal(priv->queued_requests, chan);
	g_hash_table_insert(priv->queued_requests, chan, g_slist_prepend(reqs, request));
}

static GSList *take_request_tokens(IdleMUCManager *manager, IdleMUCChannel *chan) {
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(manager);
	GSList *reqs = g_hash_table_lookup(priv->queued_requests, chan);

	g_hash_table_steal(priv->queued_requests, chan);

	return g_slist_reverse(reqs);
}

static void _channel_closed_cb(IdleMUCChannel *chan, gpointer user_data) {
	IdleMUCManager *manager = IDLE_MUC_MANAGER(user_data);
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(manager);
	TpBaseChannel *base = TP_BASE_CHANNEL (chan);
	GSList *reqs = take_request_tokens(user_data, chan);

	/* If there are any tokens for this channel when it closes, the request
	 * didn't finish before we killed the channel.
	 */
	for (GSList *l = reqs; l != NULL; l = l->next) {
		tp_channel_manager_emit_request_failed(manager, l->data, TP_ERROR,
			TP_ERROR_DISCONNECTED,
			"Unable to complete this channel request, we're disconnecting!");
	}

	g_slist_free(reqs);

	tp_channel_manager_emit_channel_closed_for_object (manager,
		TP_EXPORTABLE_CHANNEL (chan));

	if (priv->channels) {
		TpHandle handle = tp_base_channel_get_target_handle (base);

		if (tp_base_channel_is_destroyed (base))
			g_hash_table_remove(priv->channels, GUINT_TO_POINTER(handle));
		else
			tp_channel_manager_emit_new_channel (manager, TP_EXPORTABLE_CHANNEL (chan),
				NULL);
	}
}

static void _channel_join_ready_cb(IdleMUCChannel *chan, guint err, gpointer user_data) {
	TpChannelManager *manager = TP_CHANNEL_MANAGER(user_data);
	IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE(user_data);
	GSList *reqs = take_request_tokens(user_data, chan);
	gint err_code = 0;
	const gchar* err_msg = NULL;
	TpHandle handle;
	GSList *l;

	if (err == MUC_CHANNEL_JOIN_ERROR_NONE) {
		tp_channel_manager_emit_new_channel(manager, (TpExportableChannel *) chan, reqs);
		goto out;
	}

	g_object_get(chan, "handle", &handle, NULL);

	switch (err) {
		case MUC_CHANNEL_JOIN_ERROR_BANNED:
			err_code = TP_ERROR_CHANNEL_BANNED;
			err_msg = "You are banned from the channel.";
			break;

		case MUC_CHANNEL_JOIN_ERROR_FULL:
			err_code = TP_ERROR_CHANNEL_FULL;
			err_msg = "The channel is full.";
			break;

		case MUC_CHANNEL_JOIN_ERROR_INVITE_ONLY:
			err_code = TP_ERROR_CHANNEL_INVITE_ONLY;
			err_msg = "The channel is invite only.";
			break;

		default:
			g_assert_not_reached();
			break;
	}

	for (l = reqs; reqs != NULL; reqs = reqs->next) {
		tp_channel_manager_emit_request_failed(manager, l->data, TP_ERROR, err_code, err_msg);
	}

	if (priv->channels)
		g_hash_table_remove(priv->channels, GUINT_TO_POINTER(handle));

out:
	g_slist_free (reqs);
}

static gboolean
_muc_manager_request (
    IdleMUCManager *self,
    gpointer request_token,
    GHashTable *request_properties,
    gboolean require_new)
{
  IdleMUCManagerPrivate *priv = IDLE_MUC_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_ROOM);
  GError *error = NULL;
  TpHandleType handle_type;
  TpHandle handle;
  const gchar *channel_type;
  IdleMUCChannel *channel;

  channel_type = tp_asv_get_string (request_properties,
      TP_PROP_CHANNEL_CHANNEL_TYPE);

  if (tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TEXT))
    return FALSE;

  handle_type = tp_asv_get_uint32 (request_properties,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL);

  switch (handle_type)
    {
      case TP_HANDLE_TYPE_ROOM:
        handle = tp_asv_get_uint32 (request_properties,
            TP_PROP_CHANNEL_TARGET_HANDLE, NULL);

        if (!tp_handle_is_valid (room_repo, handle, &error))
          goto error;

        break;

      case TP_HANDLE_TYPE_NONE:
      {
        const gchar *room_name = tp_asv_get_string (request_properties,
            TP_PROP_CHANNEL_INTERFACE_ROOM_ROOM_NAME);

        if (room_name == NULL)
          return FALSE;

        handle = tp_handle_ensure (room_repo, room_name, NULL, &error);
        if (handle == 0)
          goto error;

        break;
      }

      default:
        return FALSE;
    }

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
        muc_channel_fixed_properties, muc_channel_all_allowed_properties,
        &error))
    goto error;

  channel = g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle));

  if (channel != NULL)
    {
      if (require_new)
        {
          g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
                 "That channel has already been created (or requested)");
          goto error;
        }
      else if (idle_muc_channel_is_ready (channel))
        {
          tp_channel_manager_emit_request_already_satisfied (self,
              request_token, TP_EXPORTABLE_CHANNEL (channel));
          return TRUE;
        }
    }
  else
    {
      channel = _muc_manager_new_channel (self, handle, base_conn->self_handle, TRUE);
      idle_muc_channel_join_attempt (channel);
    }

  associate_request (self, channel, request_token);

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

static gboolean
_muc_manager_create_channel (
    TpChannelManager *manager,
    gpointer request_token,
    GHashTable *request_properties)
{
  IdleMUCManager *self = IDLE_MUC_MANAGER (manager);

  return _muc_manager_request (self, request_token, request_properties, TRUE);
}

static gboolean
_muc_manager_request_channel (
  TpChannelManager *manager,
  gpointer request_token,
  GHashTable *request_properties)
{
  IdleMUCManager *self = IDLE_MUC_MANAGER (manager);

  return _muc_manager_request (self, request_token, request_properties, FALSE);
}

static gboolean
_muc_manager_ensure_channel (
    TpChannelManager *manager,
    gpointer request_token,
    GHashTable *request_properties)
{
  IdleMUCManager *self = IDLE_MUC_MANAGER (manager);

  return _muc_manager_request (self, request_token, request_properties, FALSE);
}

static void
_muc_manager_iface_init (
    gpointer g_iface,
    gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = _muc_manager_foreach_channel;
  iface->type_foreach_channel_class = _muc_manager_type_foreach_channel_class;
  iface->request_channel = _muc_manager_request_channel;
  iface->create_channel = _muc_manager_create_channel;
  iface->ensure_channel = _muc_manager_ensure_channel;
}

