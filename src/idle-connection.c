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

#include "idle-connection.h"

#include <config.h>

/* strnlen */
#define __USE_GNU
#include <string.h>
#include <time.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-connection.h>
#include <telepathy-glib/channel-factory-iface.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_CONNECTION
#include "idle-ctcp.h"
#include "idle-debug.h"
#include "idle-handles.h"
#include "idle-im-factory.h"
#include "idle-muc-factory.h"
#include "idle-parser.h"
#include "idle-server-connection.h"
#include "idle-server-connection-iface.h"
#include "idle-ssl-server-connection.h"

#include "extensions/extensions.h"    /* Renaming */

/* From RFC 2813 :
 * This in essence means that the client may send one (1) message every
 * two (2) seconds without being adversely affected.  Services MAY also
 * be subject to this mechanism.
 */

#define MSG_QUEUE_UNLOAD_AT_A_TIME 1
#define MSG_QUEUE_TIMEOUT 2

#define SERVER_CMD_MIN_PRIORITY 0
#define SERVER_CMD_NORMAL_PRIORITY G_MAXUINT/2
#define SERVER_CMD_MAX_PRIORITY G_MAXUINT

/* FIXME use this from telepathy-glib as soon as it gets there */
#define IDLE_TP_ALIAS_PAIR_TYPE (dbus_g_type_get_struct ("GValueArray", \
			G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID))

static void _aliasing_iface_init(gpointer, gpointer);
static void _renaming_iface_init(gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(IdleConnection, idle_connection, TP_TYPE_BASE_CONNECTION,
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING, _aliasing_iface_init);
		G_IMPLEMENT_INTERFACE(IDLE_TYPE_SVC_CONNECTION_INTERFACE_RENAMING, _renaming_iface_init));

typedef struct _IdleOutputPendingMsg IdleOutputPendingMsg;
struct _IdleOutputPendingMsg {
	gchar *message;
	guint priority;
};

#define idle_output_pending_msg_new() \
	(g_slice_new(IdleOutputPendingMsg))
#define idle_output_pending_msg_new0() \
	(g_slice_new0(IdleOutputPendingMsg))

static void idle_output_pending_msg_free(IdleOutputPendingMsg *msg) {
	if (!msg)
		return;

	g_free(msg->message);
	g_slice_free(IdleOutputPendingMsg, msg);
}

static gint pending_msg_compare(gconstpointer a, gconstpointer b, gpointer unused) {
	const IdleOutputPendingMsg *msg1 = a, *msg2 = b;

	return (msg1->priority > msg2->priority) ? -1 : 1;
}

enum {
	PROP_NICKNAME = 1,
	PROP_SERVER,
	PROP_PORT,
	PROP_PASSWORD,
	PROP_REALNAME,
	PROP_CHARSET,
	PROP_QUITMESSAGE,
	PROP_USE_SSL,
	LAST_PROPERTY_ENUM
};

/* private structure */
typedef struct _IdleConnectionPrivate IdleConnectionPrivate;
struct _IdleConnectionPrivate {
	/*
	 * network connection
	 */

	IdleServerConnectionIface *conn;
	guint sconn_status;

	/* IRC connection properties */
	char *nickname;
	char *server;
	guint port;
	char *password;
	char *realname;
	char *charset;
	char *quit_message;
	gboolean use_ssl;

	/* output message queue */
	GQueue *msg_queue;

	/* UNIX time the last message was sent on */
	time_t last_msg_sent;

	/* GSource id for message queue unloading timeout */
	guint msg_queue_timeout;

	/* if we are quitting asynchronously */
	gboolean quitting;

	/* AliasChanged aggregation */
	GPtrArray *queued_aliases;
	TpHandleSet *queued_aliases_owners;

	/* if idle_connection_dispose has already run once */
	gboolean dispose_has_run;
};

#define IDLE_CONNECTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), IDLE_TYPE_CONNECTION, IdleConnectionPrivate))

static void _iface_create_handle_repos(TpBaseConnection *self, TpHandleRepoIface **repos);
static GPtrArray *_iface_create_channel_factories(TpBaseConnection *self);
static gchar *_iface_get_unique_connection_name(TpBaseConnection *self);
static void _iface_disconnected(TpBaseConnection *self);
static void _iface_shut_down(TpBaseConnection *self);
static gboolean _iface_start_connecting(TpBaseConnection *self, GError **error);

static IdleParserHandlerResult _erroneous_nickname_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _nick_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _nickname_in_use_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _ping_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _version_privmsg_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _welcome_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);

static void sconn_status_changed_cb(IdleServerConnectionIface *sconn, IdleServerConnectionState state, IdleServerConnectionStateReason reason, IdleConnection *conn);
static void sconn_received_cb(IdleServerConnectionIface *sconn, gchar *raw_msg, IdleConnection *conn);

static void irc_handshakes(IdleConnection *conn);
static void send_quit_request(IdleConnection *conn);
static void connection_connect_cb(IdleConnection *conn, gboolean success, TpConnectionStatusReason fail_reason);
static void connection_disconnect_cb(IdleConnection *conn, TpConnectionStatusReason reason);
static gboolean idle_connection_hton(IdleConnection *obj, const gchar *input, gchar **output, GError **_error);
static void idle_connection_ntoh(IdleConnection *obj, const gchar *input, gchar **output);

static void idle_connection_init(IdleConnection *obj) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	priv->sconn_status = SERVER_CONNECTION_STATE_NOT_CONNECTED;
	priv->msg_queue = g_queue_new();
}

static GObject *idle_connection_constructor(GType type, guint n_params, GObjectConstructParam *params) {
	IdleConnection *self = IDLE_CONNECTION(G_OBJECT_CLASS(idle_connection_parent_class)->constructor(type, n_params, params));

	self->parser = g_object_new(IDLE_TYPE_PARSER, "connection", self, NULL);

	return (GObject *) self;
}

static void idle_connection_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	switch (prop_id) {
		case PROP_NICKNAME:
			g_free(priv->nickname);
			priv->nickname = g_value_dup_string(value);
			break;

		case PROP_SERVER:
			g_free(priv->server);
			priv->server = g_value_dup_string(value);
			break;

		case PROP_PORT:
			priv->port = g_value_get_uint(value);
			break;

		case PROP_PASSWORD:
			g_free(priv->password);
			priv->password = g_value_dup_string(value);
			break;

		case PROP_REALNAME:
			g_free(priv->realname);
			priv->realname = g_value_dup_string(value);
			break;

		case PROP_CHARSET:
			g_free(priv->charset);
			priv->charset = g_value_dup_string(value);
			break;

		case PROP_QUITMESSAGE:
			g_free(priv->quit_message);
			priv->quit_message = g_value_dup_string(value);
			break;

		case PROP_USE_SSL:
			priv->use_ssl = g_value_get_boolean(value);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
			break;
	}
}

static void idle_connection_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	switch (prop_id) {
		case PROP_NICKNAME:
			g_value_set_string(value, priv->nickname);
			break;

		case PROP_SERVER:
			g_value_set_string(value, priv->server);
			break;

		case PROP_PORT:
			g_value_set_uint(value, priv->port);
			break;

		case PROP_PASSWORD:
			g_value_set_string(value, priv->password);
			break;

		case PROP_REALNAME:
			g_value_set_string(value, priv->realname);
			break;

		case PROP_CHARSET:
			g_value_set_string(value, priv->charset);
			break;

		case PROP_QUITMESSAGE:
			g_value_set_string(value, priv->quit_message);
			break;

		case PROP_USE_SSL:
			g_value_set_boolean(value, priv->use_ssl);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
			break;
	}
}

static void idle_connection_dispose (GObject *object) {
	IdleConnection *self = IDLE_CONNECTION(object);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(self);

	if (priv->dispose_has_run)
		return;

	priv->dispose_has_run = TRUE;

	if (priv->msg_queue_timeout)
		g_source_remove(priv->msg_queue_timeout);

	if (priv->conn != NULL) {
		g_object_unref(priv->conn);
		priv->conn = NULL;
	}

	if (priv->queued_aliases_owners)
		tp_handle_set_destroy(priv->queued_aliases_owners);

	if (priv->queued_aliases)
		g_ptr_array_free(priv->queued_aliases, TRUE);

	g_object_unref(self->parser);

	if (G_OBJECT_CLASS(idle_connection_parent_class)->dispose)
		G_OBJECT_CLASS(idle_connection_parent_class)->dispose (object);
}

static void idle_connection_finalize (GObject *object) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(object);
	IdleOutputPendingMsg *msg;

	g_free(priv->nickname);
	g_free(priv->server);
	g_free(priv->password);
	g_free(priv->realname);
	g_free(priv->charset);
	g_free(priv->quit_message);

	while ((msg = g_queue_pop_head(priv->msg_queue)) != NULL)
		idle_output_pending_msg_free(msg);

	g_queue_free(priv->msg_queue);

	G_OBJECT_CLASS(idle_connection_parent_class)->finalize(object);
}

static void idle_connection_class_init(IdleConnectionClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	TpBaseConnectionClass *parent_class = TP_BASE_CONNECTION_CLASS(klass);
	GParamSpec *param_spec;
	static const gchar *interfaces_always_present[] = {
		TP_IFACE_CONNECTION_INTERFACE_ALIASING,
		IDLE_IFACE_CONNECTION_INTERFACE_RENAMING,
		NULL};

	g_type_class_add_private(klass, sizeof(IdleConnectionPrivate));

	object_class->constructor = idle_connection_constructor;
	object_class->set_property = idle_connection_set_property;
	object_class->get_property = idle_connection_get_property;
	object_class->dispose = idle_connection_dispose;
	object_class->finalize = idle_connection_finalize;

	parent_class->create_handle_repos = _iface_create_handle_repos;
	parent_class->get_unique_connection_name = _iface_get_unique_connection_name;
	parent_class->create_channel_factories = _iface_create_channel_factories;
	parent_class->connecting = NULL;
	parent_class->connected = NULL;
	parent_class->disconnected = _iface_disconnected;
	parent_class->shut_down = _iface_shut_down;
	parent_class->start_connecting = _iface_start_connecting;
	parent_class->interfaces_always_present = interfaces_always_present;

	param_spec = g_param_spec_string("nickname", "IRC nickname", "The nickname to be visible to others in IRC.", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB);
	g_object_class_install_property(object_class, PROP_NICKNAME, param_spec);

	param_spec = g_param_spec_string("server", "Hostname or IP of the IRC server to connect to", "The server used when establishing the connection.", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB);
	g_object_class_install_property(object_class, PROP_SERVER, param_spec);

	param_spec = g_param_spec_uint("port", "IRC server port", "The destination port used when establishing the connection.", 0, G_MAXUINT16, 0, G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_CONSTRUCT);
	g_object_class_install_property(object_class, PROP_PORT, param_spec);

	param_spec = g_param_spec_string("password", "Server password", "Password to authenticate to the server with", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB);
	g_object_class_install_property(object_class, PROP_PASSWORD, param_spec);

	param_spec = g_param_spec_string("realname", "Real name", "The real name of the user connecting to IRC", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB);
	g_object_class_install_property(object_class, PROP_REALNAME, param_spec);

	param_spec = g_param_spec_string("charset", "Character set", "The character set to use to communicate with the outside world", "NULL", G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_CONSTRUCT);
	g_object_class_install_property(object_class, PROP_CHARSET, param_spec);

	param_spec = g_param_spec_string("quit-message", "Quit message", "The quit message to send to the server when leaving IRC", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_CONSTRUCT);
	g_object_class_install_property(object_class, PROP_QUITMESSAGE, param_spec);

	param_spec = g_param_spec_boolean("use-ssl", "Use SSL", "If the connection should use a SSL tunneled socket connection", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_CONSTRUCT);
	g_object_class_install_property(object_class, PROP_USE_SSL, param_spec);
}

static GPtrArray *_iface_create_channel_factories(TpBaseConnection *self) {
	GPtrArray *factories = g_ptr_array_sized_new(1);
	GObject *factory;

	factory = g_object_new(IDLE_TYPE_IM_FACTORY, "connection", self, NULL);
	g_ptr_array_add(factories, factory);

	factory = g_object_new(IDLE_TYPE_MUC_FACTORY, "connection", self, NULL);
	g_ptr_array_add(factories, factory);

	return factories;
}

static void _iface_create_handle_repos(TpBaseConnection *self, TpHandleRepoIface **repos) {
	for (int i = 0; i < NUM_TP_HANDLE_TYPES; i++)
		repos[i] = NULL;

	idle_handle_repos_init(repos);
}

static gchar *_iface_get_unique_connection_name(TpBaseConnection *self) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(self);

	return g_strdup_printf("%s@%s", priv->nickname, priv->server);
}

static gboolean _finish_shutdown_idle_func(gpointer data) {
	TpBaseConnection *conn = TP_BASE_CONNECTION(data);

	tp_base_connection_finish_shutdown(conn);

	return FALSE;
}

static void _iface_disconnected(TpBaseConnection *self) {
	IdleConnection *conn = IDLE_CONNECTION(self);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	send_quit_request(conn);

	priv->quitting = TRUE;
}

static void _iface_shut_down(TpBaseConnection *self) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(self);
	GError *error;

	if (priv->quitting)
		return;

	if (priv->sconn_status != SERVER_CONNECTION_STATE_NOT_CONNECTED) {
		if (!idle_server_connection_iface_disconnect(priv->conn, &error))
			g_error_free(error);
	} else {
		g_idle_add(_finish_shutdown_idle_func, self);;
	}
}

static gboolean _iface_start_connecting(TpBaseConnection *self, GError **error) {
	IdleConnection *conn = IDLE_CONNECTION(self);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	g_assert(priv->nickname != NULL);
	g_assert(priv->server != NULL);
	g_assert(priv->port > 0 && priv->port <= G_MAXUINT16);

	if (priv->conn == NULL) {
		GError *conn_error = NULL;
		IdleServerConnectionIface *sconn;
		GType connection_type = (priv->use_ssl) ? IDLE_TYPE_SSL_SERVER_CONNECTION : IDLE_TYPE_SERVER_CONNECTION;

		if (!priv->realname || !priv->realname[0]) {
			const gchar *g_realname = g_get_real_name();

			g_free(priv->realname);

			if (g_realname && g_realname[0] && strcmp(g_realname, "Unknown"))
				priv->realname = g_strdup(g_realname);
			else
				priv->realname = g_strdup(priv->nickname);
		}

		sconn = IDLE_SERVER_CONNECTION_IFACE(g_object_new(connection_type, "host", priv->server, "port", priv->port, NULL));

		g_signal_connect(sconn, "status-changed", (GCallback)(sconn_status_changed_cb), conn);

		if (!idle_server_connection_iface_connect(sconn, &conn_error)) {
			IDLE_DEBUG("server connection failed to connect: %s", conn_error->message);
			g_set_error(error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, "failed to open low-level network connection: %s", conn_error->message);

			g_error_free(conn_error);
			g_object_unref(sconn);

			return FALSE;
		}

		priv->conn = sconn;

		g_signal_connect(sconn, "received", (GCallback)(sconn_received_cb), conn);

		idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_ERRONEOUSNICKNAME, _erroneous_nickname_handler, conn);
		idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_NICKNAMEINUSE, _nickname_in_use_handler, conn);
		idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_WELCOME, _welcome_handler, conn);

		idle_parser_add_handler(conn->parser, IDLE_PARSER_CMD_PING, _ping_handler, conn);

		idle_parser_add_handler_with_priority(conn->parser, IDLE_PARSER_PREFIXCMD_NICK, _nick_handler, conn, IDLE_PARSER_HANDLER_PRIORITY_FIRST);
		idle_parser_add_handler(conn->parser, IDLE_PARSER_PREFIXCMD_PRIVMSG_USER, _version_privmsg_handler, conn);

		irc_handshakes(conn);
	} else {
		IDLE_DEBUG("conn already open!");

		g_set_error(error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "connection already open!");

		return FALSE;
	}

	return TRUE;
}

static gboolean msg_queue_timeout_cb(gpointer user_data);

static void sconn_status_changed_cb(IdleServerConnectionIface *sconn, IdleServerConnectionState state, IdleServerConnectionStateReason reason, IdleConnection *conn) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	TpConnectionStatusReason tp_reason;

	IDLE_DEBUG("called with state %u", state);

	switch (reason) {
		case SERVER_CONNECTION_STATE_REASON_ERROR:
			tp_reason = TP_CONNECTION_STATUS_REASON_NETWORK_ERROR;
			break;

		case SERVER_CONNECTION_STATE_REASON_REQUESTED:
			tp_reason = TP_CONNECTION_STATUS_REASON_REQUESTED;
			break;

		default:
			g_assert_not_reached();
			break;
	}

	if (priv->quitting)
		tp_reason = TP_CONNECTION_STATUS_REASON_REQUESTED;

	switch (state) {
		case SERVER_CONNECTION_STATE_NOT_CONNECTED:
			if (conn->parent.status == TP_CONNECTION_STATUS_CONNECTING) {
				connection_connect_cb(conn, FALSE, TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
				connection_disconnect_cb(conn, tp_reason);
			}
			else {
				connection_disconnect_cb(conn, tp_reason);
			}
			break;

		case SERVER_CONNECTION_STATE_CONNECTING:
			break;

		case SERVER_CONNECTION_STATE_CONNECTED:
			if ((priv->msg_queue_timeout == 0) && (g_queue_get_length(priv->msg_queue) > 0)) {
				IDLE_DEBUG("we had messages in queue, start unloading them now");

				priv->msg_queue_timeout = g_timeout_add(MSG_QUEUE_TIMEOUT, msg_queue_timeout_cb, conn);
			}
			break;

		default:
			g_assert_not_reached();
			break;
	}

	priv->sconn_status = state;
}

static void sconn_received_cb(IdleServerConnectionIface *sconn, gchar *raw_msg, IdleConnection *conn) {
	gchar *converted;

	idle_connection_ntoh(conn, raw_msg, &converted);
	idle_parser_receive(conn->parser, converted);

	g_free(converted);
}

static gboolean msg_queue_timeout_cb(gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	int i, j;
	IdleOutputPendingMsg *output_msg;
	gchar msg[IRC_MSG_MAXLEN + 3];
	GError *error;

	IDLE_DEBUG("called");

	if (priv->sconn_status != SERVER_CONNECTION_STATE_CONNECTED) {
		IDLE_DEBUG("connection was not connected!");

		priv->msg_queue_timeout = 0;

		return FALSE;
	}

	output_msg = g_queue_peek_head(priv->msg_queue);

	if (output_msg == NULL) {
		priv->msg_queue_timeout = 0;
		return FALSE;
	}

	g_strlcpy(msg, output_msg->message, IRC_MSG_MAXLEN + 3);

	for (i = 1; i < MSG_QUEUE_UNLOAD_AT_A_TIME; i++) {
		output_msg = g_queue_peek_nth(priv->msg_queue, i);

		if ((output_msg != NULL) && ((strlen(msg) + strlen(output_msg->message)) < IRC_MSG_MAXLEN + 2))
			strcat(msg, output_msg->message);
		else
			break;
	}

	if (idle_server_connection_iface_send(priv->conn, msg, &error)) {
		for (j = 0; j < i; j++) {
			output_msg = g_queue_pop_head(priv->msg_queue);
			idle_output_pending_msg_free(output_msg);
		}

		priv->last_msg_sent = time(NULL);
	} else {
		IDLE_DEBUG("low-level network connection failed to send: %s", error->message);

		g_error_free(error);
	}

	return TRUE;
}

/**
 * Queue a IRC command for sending, clipping it to IRC_MSG_MAXLEN bytes and appending the required <CR><LF> to it
 */
static void _send_with_priority(IdleConnection *conn, const gchar *msg, guint priority) {
	gchar cmd[IRC_MSG_MAXLEN + 3];
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	int len;
	GError *error;
	gchar *converted;
	GError *convert_error;
	time_t curr_time = time(NULL);
	IdleOutputPendingMsg *output_msg;

	g_assert(msg != NULL);

	/* Clip the message */

	g_strlcpy(cmd, msg, IRC_MSG_MAXLEN + 1);

	/* Append <CR><LF> */

	len = strlen(cmd);
	cmd[len++] = '\r';
	cmd[len++] = '\n';

	cmd[len] = '\0';

	if (!idle_connection_hton(conn, cmd, &converted, &convert_error)) {
		IDLE_DEBUG("hton: %s", convert_error->message);
		g_error_free(convert_error);
		converted = g_strdup(cmd);
	}

	if ((priority == SERVER_CMD_MAX_PRIORITY) || ((conn->parent.status == TP_CONNECTION_STATUS_CONNECTED)	&& (priv->msg_queue_timeout == 0)	&& (curr_time - priv->last_msg_sent > MSG_QUEUE_TIMEOUT))) {
		priv->last_msg_sent = curr_time;

		if (!idle_server_connection_iface_send(priv->conn, converted, &error)) {
			IDLE_DEBUG("server connection failed to send: %s", error->message);
			g_error_free(error);
		} else {
			g_free(converted);
			return;
		}
	}

	output_msg = idle_output_pending_msg_new();
	output_msg->message = converted;
	output_msg->priority = priority;

	g_queue_insert_sorted(priv->msg_queue, output_msg, pending_msg_compare, NULL);

	if (!priv->msg_queue_timeout)
		priv->msg_queue_timeout = g_timeout_add(MSG_QUEUE_TIMEOUT * 1000, msg_queue_timeout_cb, conn);
}

void idle_connection_send(IdleConnection *conn, const gchar *msg) {
	return _send_with_priority(conn, msg, SERVER_CMD_NORMAL_PRIORITY);
}

static IdleParserHandlerResult _erroneous_nickname_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);

	if (conn->parent.status == TP_CONNECTION_STATUS_CONNECTING)
		connection_connect_cb(conn, FALSE, TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _nick_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	TpHandle old_handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandle new_handle = g_value_get_uint(g_value_array_get_nth(args, 1));

	if (old_handle == new_handle)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	if (old_handle == conn->parent.self_handle) {
		TpHandleRepoIface *handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(conn), TP_HANDLE_TYPE_CONTACT);

		tp_handle_unref(handles, conn->parent.self_handle);
		conn->parent.self_handle = new_handle;
		tp_handle_ref(handles, new_handle);
	}

	idle_svc_connection_interface_renaming_emit_renamed(IDLE_SVC_CONNECTION_INTERFACE_RENAMING(conn), old_handle, new_handle);

	idle_connection_emit_queued_aliases_changed(conn);

	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _nickname_in_use_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);

	if (conn->parent.status == TP_CONNECTION_STATUS_CONNECTING)
		connection_connect_cb(conn, FALSE, TP_CONNECTION_STATUS_REASON_NAME_IN_USE);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _ping_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);

	gchar *reply = g_strdup_printf("PONG %s", g_value_get_string(g_value_array_get_nth(args, 0)));
	_send_with_priority(conn, reply, SERVER_CMD_MAX_PRIORITY);
	g_free(reply);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _version_privmsg_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	const gchar *msg = g_value_get_string(g_value_array_get_nth(args, 2));

	if (g_ascii_strcasecmp(msg, "\001VERSION\001"))
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	TpHandle handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	const gchar *nick = tp_handle_inspect(tp_base_connection_get_handles(TP_BASE_CONNECTION(conn), TP_HANDLE_TYPE_CONTACT), handle);
	gchar *reply = g_strdup_printf("VERSION telepathy-idle %s Telepathy IM/VoIP Framework http://telepathy.freedesktop.org", VERSION);

	idle_ctcp_notice(nick, reply, conn);

	g_free(reply);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _welcome_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	TpHandle handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandleRepoIface *handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(conn), TP_HANDLE_TYPE_CONTACT);

	conn->parent.self_handle = handle;
	tp_handle_ref(handles, handle);
	g_assert(tp_handle_is_valid(handles, handle, NULL));

	connection_connect_cb(conn, TRUE, 0);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static void irc_handshakes(IdleConnection *conn) {
	IdleConnectionPrivate *priv;
	gchar msg[IRC_MSG_MAXLEN + 1];

	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	if ((priv->password != NULL) && (priv->password[0] != '\0')) {
		g_snprintf(msg, IRC_MSG_MAXLEN + 1, "PASS %s", priv->password);
		_send_with_priority(conn, msg, SERVER_CMD_NORMAL_PRIORITY + 1);
	}

	g_snprintf(msg, IRC_MSG_MAXLEN + 1, "NICK %s", priv->nickname);
	idle_connection_send(conn, msg);

	g_snprintf(msg, IRC_MSG_MAXLEN + 1, "USER %s %u * :%s", priv->nickname, 8, priv->realname);
	idle_connection_send(conn, msg);
}

static void send_quit_request(IdleConnection *conn) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	gchar cmd[IRC_MSG_MAXLEN + 1];

	g_snprintf(cmd, IRC_MSG_MAXLEN + 1, "QUIT :%s", priv->quit_message);

	_send_with_priority(conn, cmd, SERVER_CMD_MAX_PRIORITY);
}

static void connection_connect_cb(IdleConnection *conn, gboolean success, TpConnectionStatusReason fail_reason) {
	TpBaseConnection *base = TP_BASE_CONNECTION(conn);

	if (success)
		tp_base_connection_change_status(base, TP_CONNECTION_STATUS_CONNECTED, TP_CONNECTION_STATUS_REASON_REQUESTED);
	else
		tp_base_connection_change_status(base, TP_CONNECTION_STATUS_DISCONNECTED, fail_reason);
}

static void connection_disconnect_cb(IdleConnection *conn, TpConnectionStatusReason reason) {
	TpBaseConnection *base = TP_BASE_CONNECTION(conn);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	if (base->status == TP_CONNECTION_STATUS_DISCONNECTED)
		g_idle_add(_finish_shutdown_idle_func, base);
	else
		tp_base_connection_change_status(base, TP_CONNECTION_STATUS_DISCONNECTED, reason);

	if (priv->msg_queue_timeout) {
		g_source_remove(priv->msg_queue_timeout);
		priv->msg_queue_timeout = 0;
	}
}

void _queue_alias_changed(IdleConnection *conn, TpHandle handle, const gchar *alias) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	if (!priv->queued_aliases_owners) {
		TpHandleRepoIface *handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(conn), TP_HANDLE_TYPE_CONTACT);
		priv->queued_aliases_owners = tp_handle_set_new(handles);
	}

	tp_handle_set_add(priv->queued_aliases_owners, handle);

	if (!priv->queued_aliases)
		priv->queued_aliases = g_ptr_array_new();

	GValue value = {0, };

	g_value_init(&value, IDLE_TP_ALIAS_PAIR_TYPE);
	g_value_take_boxed(&value, dbus_g_type_specialized_construct(IDLE_TP_ALIAS_PAIR_TYPE));

	dbus_g_type_struct_set(&value,
			0, handle,
			1, alias,
			G_MAXUINT);

	g_ptr_array_add(priv->queued_aliases, g_value_get_boxed(&value));
}

static GQuark _canon_nick_quark() {
	static GQuark quark = 0;

	if (!quark)
		quark = g_quark_from_static_string("canon-nick");

	return quark;
}

void idle_connection_canon_nick_receive(IdleConnection *conn, TpHandle handle, const gchar *canon_nick) {
	TpHandleRepoIface *handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(conn), TP_HANDLE_TYPE_CONTACT);
	const gchar *old_alias = tp_handle_get_qdata(handles, handle, _canon_nick_quark());

	if (!old_alias)
		old_alias = tp_handle_inspect(handles, handle);

	if (!strcmp(old_alias, canon_nick))
		return;

	tp_handle_set_qdata(handles, handle, _canon_nick_quark(), g_strdup(canon_nick), g_free);

	_queue_alias_changed(conn, handle, canon_nick);
}

void idle_connection_emit_queued_aliases_changed(IdleConnection *conn) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	if (!priv->queued_aliases)
		return;

	tp_svc_connection_interface_aliasing_emit_aliases_changed(conn, priv->queued_aliases);

	g_ptr_array_free(priv->queued_aliases, TRUE);
	priv->queued_aliases = NULL;

	tp_handle_set_destroy(priv->queued_aliases_owners);
	priv->queued_aliases_owners = NULL;
}

static void idle_connection_get_alias_flags(TpSvcConnectionInterfaceAliasing *iface, DBusGMethodInvocation *context) {
	tp_svc_connection_interface_aliasing_return_from_get_alias_flags(context, 0);
}

static void idle_connection_request_aliases(TpSvcConnectionInterfaceAliasing *iface, const GArray *handles, DBusGMethodInvocation *context) {
	TpHandleRepoIface *repo = tp_base_connection_get_handles(TP_BASE_CONNECTION(iface), TP_HANDLE_TYPE_CONTACT);
	GError *error = NULL;

	if (!tp_handles_are_valid(repo, handles, FALSE, &error)) {
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return;
	}

	const gchar **aliases = g_new0(const gchar *, handles->len + 1);
	for (int i = 0; i < handles->len; i++) {
		TpHandle handle = g_array_index(handles, TpHandle, i);

		const gchar *alias = tp_handle_get_qdata(repo, handle, _canon_nick_quark());
		if (!alias)
			alias = tp_handle_inspect(repo, handle);

		aliases[i] = alias;
	}

	tp_svc_connection_interface_aliasing_return_from_request_aliases(context, aliases);

	g_free(aliases);
}

static gboolean _send_rename_request(IdleConnection *obj, const gchar *nick, DBusGMethodInvocation *context) {
	TpHandleRepoIface *handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(obj), TP_HANDLE_TYPE_CONTACT);
	TpHandle handle = tp_handle_ensure(handles, nick, NULL, NULL);

	if (handle == 0) {
		IDLE_DEBUG("failed to get handle for \"%s\"", nick);

		GError error = {TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "Invalid nickname requested"};
		dbus_g_method_return_error(context, &error);

		return FALSE;
	}

	tp_handle_unref(handles, handle);

	gchar msg[IRC_MSG_MAXLEN + 1];
	g_snprintf(msg, IRC_MSG_MAXLEN + 1, "NICK %s", nick);
	idle_connection_send(obj, msg);

	return TRUE;
}

static void idle_connection_request_rename(IdleSvcConnectionInterfaceRenaming *iface, const gchar *nick, DBusGMethodInvocation *context) {
	IdleConnection *conn = IDLE_CONNECTION(iface);

	if (_send_rename_request(conn, nick, context))
		idle_svc_connection_interface_renaming_return_from_request_rename(context);
}

static void idle_connection_set_aliases(TpSvcConnectionInterfaceAliasing *iface, GHashTable *aliases, DBusGMethodInvocation *context) {
	IdleConnection *conn = IDLE_CONNECTION(iface);
	const gchar *requested_alias = g_hash_table_lookup(aliases, GUINT_TO_POINTER(conn->parent.self_handle));

	if ((g_hash_table_size(aliases) != 1) || !requested_alias) {
		GError error = {TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "You can only set your own alias in IRC"};
		dbus_g_method_return_error(context, &error);

		return;
	}

	if (_send_rename_request(conn, requested_alias, context))
		tp_svc_connection_interface_aliasing_return_from_set_aliases(context);
}

static gboolean idle_connection_hton(IdleConnection *obj, const gchar *input, gchar **output, GError **_error) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(obj);
	GError *error = NULL;
	gsize bytes_written;
	gchar *ret;

	if (input == NULL) {
		*output = NULL;
		return TRUE;
	}

	ret = g_convert(input, -1, priv->charset, "UTF-8", NULL, &bytes_written, &error);

	if (ret == NULL) {
		IDLE_DEBUG("g_convert failed: %s", error->message);
		g_set_error(_error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "character set conversion failed: %s", error->message);
		g_error_free(error);
		*output = NULL;
		return FALSE;
	}

	*output = ret;
	return TRUE;
}

static void idle_connection_ntoh(IdleConnection *obj, const gchar *input, gchar **output) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(obj);
	GError *error = NULL;
	gsize bytes_written;
	gchar *ret;
	gchar *p;

	if (input == NULL) {
		*output = NULL;
		return;
	}

	ret = g_convert(input, -1, "UTF-8", priv->charset, NULL, &bytes_written, &error);

	if (ret == NULL) {
		IDLE_DEBUG("charset conversion failed, falling back to US-ASCII: %s", error->message);
		g_error_free(error);

		ret = g_strdup(input);

		for (p = ret; *p != '\0'; p++) {
			if (*p & (1 << 7))
				*p = '?';
		}
	}

	*output = ret;
	return;
}

static void _aliasing_iface_init(gpointer g_iface, gpointer iface_data) {
	TpSvcConnectionInterfaceAliasingClass *klass = (TpSvcConnectionInterfaceAliasingClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_aliasing_implement_##x (\
		klass, idle_connection_##x)
	IMPLEMENT(get_alias_flags);
	IMPLEMENT(request_aliases);
	IMPLEMENT(set_aliases);
#undef IMPLEMENT
}

static void _renaming_iface_init(gpointer g_iface, gpointer iface_data) {
	IdleSvcConnectionInterfaceRenamingClass *klass = (IdleSvcConnectionInterfaceRenamingClass *) g_iface;

#define IMPLEMENT(x) idle_svc_connection_interface_renaming_implement_##x (\
		klass, idle_connection_##x)
	IMPLEMENT(request_rename);
#undef IMPLEMENT
}

