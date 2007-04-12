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

#define DBUS_API_SUBJECT_TO_CHANGE

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <telepathy-glib/intset.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/svc-connection.h>
#include <telepathy-glib/channel-factory-iface.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

/* strnlen */
#define __USE_GNU
#include <string.h>
#include <ctype.h>

#include <unistd.h>
#include <pthread.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "idle-handles.h"
#include "idle-muc-channel.h"

#include "idle-parser.h"
#include "idle-server-connection.h"
#include "idle-ssl-server-connection.h"
#include "idle-server-connection-iface.h"
#include "idle-im-factory.h"
#include "idle-muc-factory.h"

#include "idle-connection.h"

#include "idle-version.h"

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

static void renaming_iface_init(gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(IdleConnection, idle_connection, TP_TYPE_BASE_CONNECTION,
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_RENAMING, renaming_iface_init));

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

	/* if idle_connection_dispose has already run once */
	gboolean dispose_has_run;
};

#define IDLE_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), IDLE_TYPE_CONNECTION, IdleConnectionPrivate))

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
static IdleParserHandlerResult _welcome_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);

static void sconn_status_changed_cb(IdleServerConnectionIface *sconn, IdleServerConnectionState state, IdleServerConnectionStateReason reason, IdleConnection *conn);
static void sconn_received_cb(IdleServerConnectionIface *sconn, gchar *raw_msg, IdleConnection *conn);

static void irc_handshakes(IdleConnection *conn);
static void send_quit_request(IdleConnection *conn);
static void connection_connect_cb(IdleConnection *conn, gboolean success, TpConnectionStatusReason fail_reason);
static void connection_disconnect_cb(IdleConnection *conn, TpConnectionStatusReason reason);
static void send_irc_cmd(IdleConnection *conn, const gchar *msg);

static void idle_connection_init (IdleConnection *obj) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE (obj);

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

	g_object_unref(self->parser);

	if (G_OBJECT_CLASS (idle_connection_parent_class)->dispose)
		G_OBJECT_CLASS (idle_connection_parent_class)->dispose (object);
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

	G_OBJECT_CLASS (idle_connection_parent_class)->finalize(object);
}

static void idle_connection_class_init (IdleConnectionClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	TpBaseConnectionClass *parent_class = TP_BASE_CONNECTION_CLASS(klass);
	GParamSpec *param_spec;
	static const gchar *interfaces_always_present[] = {TP_IFACE_CONNECTION_INTERFACE_RENAMING, NULL};

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

	param_spec = g_param_spec_uint("port", "IRC server port", "The destination port used when establishing the connection.", 0, G_MAXUINT16, 6667, G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_CONSTRUCT);
	g_object_class_install_property(object_class, PROP_PORT, param_spec);

	param_spec = g_param_spec_string("password", "Server password", "Password to authenticate to the server with", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB);
	g_object_class_install_property(object_class, PROP_PASSWORD, param_spec);

	param_spec = g_param_spec_string("realname", "Real name", "The real name of the user connecting to IRC", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB);
	g_object_class_install_property(object_class, PROP_REALNAME, param_spec);

	param_spec = g_param_spec_string("charset", "Character set", "The character set to use to communicate with the outside world", "UTF-8", G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_CONSTRUCT);
	g_object_class_install_property(object_class, PROP_CHARSET, param_spec);

	param_spec = g_param_spec_string("quit-message", "Quit message", "The quit message to send to the server when leaving IRC", "So long and thanks for all the IRC - telepathy-idle IRC Connection Manager for Telepathy - http://telepathy.freedesktop.org", G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_CONSTRUCT);
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
			g_debug("%s: server connection failed to connect: %s", G_STRFUNC, conn_error->message);
			g_set_error(error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, "failed to open low-level network connection: %s", conn_error->message);

			g_error_free(conn_error);
			g_object_unref(sconn);

			return FALSE;
		}

		priv->conn = sconn;

		g_signal_connect(sconn, "received", (GCallback)(sconn_received_cb), conn);

		idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_ERRONEOUSNICKNAME, _erroneous_nickname_handler, conn);
		idle_parser_add_handler(conn->parser, IDLE_PARSER_PREFIXCMD_NICK, _nick_handler, conn);
		idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_NICKNAMEINUSE, _nickname_in_use_handler, conn);
		idle_parser_add_handler(conn->parser, IDLE_PARSER_CMD_PING, _ping_handler, conn);
		idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_WELCOME, _welcome_handler, conn);

		irc_handshakes(conn);
	}	else {
		g_debug("%s: conn already open!", G_STRFUNC);

		g_set_error(error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "connection already open!");

		return FALSE;
	}

	return TRUE;
}

static gboolean msg_queue_timeout_cb(gpointer user_data);

static void sconn_status_changed_cb(IdleServerConnectionIface *sconn, IdleServerConnectionState state, IdleServerConnectionStateReason reason, IdleConnection *conn) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	TpConnectionStatusReason tp_reason;

	g_debug("%s: called with state %u", G_STRFUNC, state);

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
				g_debug("%s: we had messages in queue, start unloading them now", G_STRFUNC);

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
}

gboolean _idle_connection_send(IdleConnection *conn, const gchar *msg) {
	send_irc_cmd(conn, msg);

	return TRUE;
}

static gboolean msg_queue_timeout_cb(gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	int i, j;
	IdleOutputPendingMsg *output_msg;
	gchar msg[IRC_MSG_MAXLEN+3];
	GError *error;

	g_debug("%s: called", G_STRFUNC);

	if (priv->sconn_status != SERVER_CONNECTION_STATE_CONNECTED) {
		g_debug("%s: connection was not connected!", G_STRFUNC);

		priv->msg_queue_timeout = 0;

		return FALSE;
	}

	output_msg = g_queue_peek_head(priv->msg_queue);

	if (output_msg == NULL) {
		priv->msg_queue_timeout = 0;
		return FALSE;
	}

	g_strlcpy(msg, output_msg->message, IRC_MSG_MAXLEN+3);

	for (i = 1; i < MSG_QUEUE_UNLOAD_AT_A_TIME; i++) {
		output_msg = g_queue_peek_nth(priv->msg_queue, i);

		if ((output_msg != NULL) && ((strlen(msg) + strlen(output_msg->message)) < IRC_MSG_MAXLEN+2))
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
	}
	else {
		g_debug("%s: low-level network connection failed to send: %s", G_STRFUNC, error->message);

		g_error_free(error);		
	}

	return TRUE;
}

/**
 * Queue a IRC command for sending, clipping it to IRC_MSG_MAXLEN bytes and appending the required <CR><LF> to it
 */
static void send_irc_cmd_full(IdleConnection *conn, const gchar *msg, guint priority) {
	gchar cmd[IRC_MSG_MAXLEN+3];
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	int len;
	GError *error;
	gchar *converted;
	GError *convert_error;
	time_t curr_time = time(NULL);
	IdleOutputPendingMsg *output_msg;

	g_assert(msg != NULL);

	/* Clip the message */

	g_strlcpy(cmd, msg, IRC_MSG_MAXLEN+1);

	/* Append <CR><LF> */

	len = strlen(cmd);
	cmd[len++] = '\r';
	cmd[len++] = '\n';

	cmd[len] = '\0';

	if (!idle_connection_hton(conn, cmd, &converted, &convert_error)) {
		g_debug("%s: hton: %s", G_STRFUNC, convert_error->message);
		g_error_free(convert_error);
		converted = g_strdup(cmd);
	}

	if ((priority == SERVER_CMD_MAX_PRIORITY) || ((conn->parent.status == TP_CONNECTION_STATUS_CONNECTED)	&& (priv->msg_queue_timeout == 0)	&& (curr_time - priv->last_msg_sent > MSG_QUEUE_TIMEOUT))) {
		priv->last_msg_sent = curr_time;

		if (!idle_server_connection_iface_send(priv->conn, converted, &error)) {
			g_debug("%s: server connection failed to send: %s", G_STRFUNC, error->message);
			g_error_free(error);
		}	else {
			g_free(converted);
			return;
		}
	}

	output_msg = idle_output_pending_msg_new();
	output_msg->message = converted;
	output_msg->priority = priority;

	g_queue_insert_sorted(priv->msg_queue, output_msg, pending_msg_compare, NULL);

	if (!priv->msg_queue_timeout)
		priv->msg_queue_timeout = g_timeout_add(MSG_QUEUE_TIMEOUT*1024, msg_queue_timeout_cb, conn);
}

static void send_irc_cmd(IdleConnection *conn, const gchar *msg) {
	return send_irc_cmd_full(conn, msg, SERVER_CMD_NORMAL_PRIORITY);	
}

static IdleParserHandlerResult _erroneous_nickname_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);

	if (conn->parent.status == TP_CONNECTION_STATUS_CONNECTING)
		connection_connect_cb(conn, FALSE, TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _nick_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	TpHandle old = g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandle new = g_value_get_uint(g_value_array_get_nth(args, 1));

	if (old == conn->parent.self_handle) {
		TpHandleRepoIface *handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(conn), TP_HANDLE_TYPE_CONTACT);

		tp_handle_unref(handles, conn->parent.self_handle);
		conn->parent.self_handle = new;
		tp_handle_ref(handles, new);

		tp_svc_connection_interface_renaming_emit_renamed((TpSvcConnectionInterfaceRenaming *) conn, old, new);
	}

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
	send_irc_cmd_full (conn, reply, SERVER_CMD_MAX_PRIORITY);
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
	gchar msg[IRC_MSG_MAXLEN+1];

	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	if ((priv->password != NULL) && (priv->password[0] != '\0')) {
		g_snprintf(msg, IRC_MSG_MAXLEN+1, "PASS %s", priv->password);
		send_irc_cmd_full(conn, msg, SERVER_CMD_NORMAL_PRIORITY + 1);
	}

	g_snprintf(msg, IRC_MSG_MAXLEN+1, "NICK %s", priv->nickname);
	send_irc_cmd(conn, msg);

	g_snprintf(msg, IRC_MSG_MAXLEN+1, "USER %s %u * :%s", priv->nickname, 8, priv->realname);
	send_irc_cmd(conn, msg);
}

static void send_quit_request(IdleConnection *conn) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	gchar cmd[IRC_MSG_MAXLEN+1];

	g_snprintf(cmd, IRC_MSG_MAXLEN+1, "QUIT :%s", priv->quit_message);

	send_irc_cmd_full(conn, cmd, SERVER_CMD_MAX_PRIORITY);
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

static void idle_connection_request_rename(TpSvcConnectionInterfaceRenaming *iface, const gchar *nick, DBusGMethodInvocation *context) {
	TpBaseConnection *base = TP_BASE_CONNECTION(iface);
	IdleConnection *obj = IDLE_CONNECTION(iface);
	TpHandleRepoIface *handles = tp_base_connection_get_handles(base, TP_HANDLE_TYPE_CONTACT);
	TpHandle handle;
	gchar msg[IRC_MSG_MAXLEN + 1];
	GError *error;

	handle = tp_handle_ensure(handles, nick, NULL, NULL);

	if (handle == 0) {
		g_debug("%s: failed to get handle for (%s)", G_STRFUNC, nick);

		error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "Failed to get handle for (%s)", nick);
		dbus_g_method_return_error(context, error);
		g_error_free(error);

		return;
	}

	tp_handle_unref(handles, handle);

	g_snprintf(msg, IRC_MSG_MAXLEN + 1, "NICK %s", nick);
	send_irc_cmd(obj, msg);

	tp_svc_connection_interface_renaming_return_from_request_rename(context);
}

gboolean idle_connection_hton(IdleConnection *obj, const gchar *input, gchar **output, GError **_error) {
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(obj);
	GError *error = NULL;
	gsize bytes_written;
	gchar *ret;

	if (input == NULL)
	{
		*output = NULL;
		return TRUE;
	}

	ret = g_convert(input, -1, priv->charset, "UTF-8", NULL, &bytes_written, &error);

	if (ret == NULL) {
		g_debug("%s: g_convert failed: %s", G_STRFUNC, error->message);
		g_set_error(_error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "character set conversion failed: %s", error->message);
		g_error_free(error);
		*output = NULL;
		return FALSE;
	}

	*output = ret;
	return TRUE;
}

void idle_connection_ntoh(IdleConnection *obj, const gchar *input, gchar **output) {
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
		g_debug("%s: charset conversion failed, falling back to US-ASCII: %s", G_STRFUNC, error->message);
		g_error_free(error);

		ret = g_strdup(input);

		for (p = ret; *p != '\0'; p++) {
			if (*p & (1<<7))
				*p = '?';
		}
	}

	*output = ret;
	return;
}

static void
renaming_iface_init(gpointer g_iface, gpointer iface_data) {
	TpSvcConnectionInterfaceRenamingClass *klass = (TpSvcConnectionInterfaceRenamingClass *)g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_renaming_implement_##x (\
		klass, idle_connection_##x)
	IMPLEMENT(request_rename);
#undef IMPLEMENT
}

#if 0
typedef struct
{
	guint old, new;
} MUCChannelRenameData;

static void muc_channel_rename_foreach(gpointer key, gpointer value, gpointer data)
{
	IdleMUCChannel *chan = IDLE_MUC_CHANNEL(value);

	MUCChannelRenameData *rename_data = (MUCChannelRenameData *)(data);

	_idle_muc_channel_rename(chan, rename_data->old, rename_data->new);
}
#endif

#if 0
static void muc_channel_handle_quit_foreach(gpointer key, gpointer value, gpointer user_data)
{
	IdleMUCChannel *chan = value;
	TpHandle handle = GPOINTER_TO_INT(user_data);

	g_debug("%s: for %p %p %p", G_STRFUNC, key, value, user_data);

	_idle_muc_channel_handle_quit(chan, handle, TRUE, handle, TP_CHANNEL_GROUP_CHANGE_REASON_OFFLINE);
}
#endif

#if 0
static gchar *prefix_cmd_parse(IdleConnection *conn, const gchar *msg)
{
	IdleConnectionPrivate *priv;
	guint tokenc;
	gchar *reply = NULL;

	gchar **tokens;
	gchar *sender = NULL, *cmd = NULL, *recipient = NULL;
	gchar *temp = NULL;
	gchar *from = NULL;
	gunichar ucs4char;

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	tokens = g_strsplit_set(msg, " ", -1);
	for (tokenc = 0; tokens[tokenc] != NULL; tokenc++) {}

	sender = tokens[0];
	cmd = tokens[1];
	recipient = tokens[2];

	if (!sender || !cmd || !recipient)
	{
		g_debug("%s: failed to parse message (%s) into sender, cmd and recipient", G_STRFUNC, msg);

		goto cleanupl;
	}

	if ((temp = strchr(sender, '!')) > sender)
	{
		from = g_strndup(sender, temp-sender);
	}
	else
	{
		if ((temp = strchr(sender, '@')) > sender)
		{
			from = g_strndup(sender, temp-sender);
		}
		else
		{
			from = sender;
		}
	}

	if ((g_strncasecmp(cmd, "NOTICE", 6) == 0) || (g_strncasecmp(cmd, "PRIVMSG", 7) == 0))
	{
		TpHandle handle;
		gchar *body;
		TpChannelTextMessageType msgtype;

		if ((body = strstr(msg, " :")+1) != NULL)
		{
			body++;
		}
		else
		{
			g_debug("%s got NOTICE/PRIVMSG with missing body identifier \" :\" (%s)", G_STRFUNC, msg);
			goto cleanupl;
		}

		if (cmd[0] == 'N')
		{
			msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE; 
		}
		else
		{
			if (body[0] == '\001')
			{
				char *suffix;

				body++;

				suffix = strrchr(body, '\001');

				g_assert(suffix != NULL);

				*suffix = '\0';

				if (!g_strncasecmp(body, "ACTION ", 7))
				{
					g_debug("%s: detected CTCP ACTION message", G_STRFUNC);

					body += 7;

					msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION;
				}
				else if (!g_strncasecmp(body, "VERSION", 7))
				{
					g_debug("%s: detected CTCP VERSION message", G_STRFUNC);

					reply = g_strdup_printf("NOTICE %s :\001VERSION %s\001", from, priv->ctcp_version_string);

					goto cleanupl;
				}
				else
				{
					g_debug("%s: ignored unimplemented (non-ACTION/VERSION) CTCP (%s)", G_STRFUNC, body);
					goto cleanupl;
				}
			}
			else
			{
				msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
			}
		}

		ucs4char = g_utf8_get_char_validated(recipient, -1);

		/*		if (g_unichar_isalpha(ucs4char))
					{
					IdleIMChannel *chan;

					if ((handle = idle_handle_for_contact(conn->handles[TP_HANDLE_TYPE_CONTACT], from)) == 0)
					{
					g_debug("%s: got NOTICE/PRIVMSG with malformed sender %s (%s)", G_STRFUNC, from, msg);
					goto cleanupl;
					}

					g_debug("%s: receiving msg with type %u from %s (handle %u): %s", G_STRFUNC, msgtype, from, handle, body);

					if ((chan = g_hash_table_lookup(priv->im_channels, GINT_TO_POINTER(handle))) == NULL)
					{
					chan = new_im_channel(conn, handle, FALSE);

					g_debug("%s: spawning new IdleIMChannel (address %p handle %u)", G_STRFUNC, chan, handle);
					}
					else
					{
					g_debug("%s: receiving thru existing IdleIMChannel %p", G_STRFUNC, chan);
					}

					_idle_im_channel_receive(chan, msgtype, handle, body);
					}
					else */if ((recipient[0] == '#') || (recipient[0] == '&') || (recipient[0] == '!') || (recipient[0] == '+'))
		{
			IdleMUCChannel *chan;
			TpHandle sender_handle;

			if ((handle = idle_handle_for_room(conn->handles[TP_HANDLE_TYPE_ROOM], recipient)) == 0)
			{
				g_debug("%s: got NOTICE/PRIVMSG with malformed IRC channel recipient %s (%s)", G_STRFUNC, from, msg);
				goto cleanupl;
			}

			if ((sender_handle = idle_handle_for_contact(conn->handles[TP_HANDLE_TYPE_CONTACT], from)) == 0)
			{
				g_debug("%s: got NOTICE/PRIVMSG with malformed sender %s (%s)", G_STRFUNC, from, msg);
				goto cleanupl;
			}

			g_debug("%s: receiving NOTICE/PRIVMSG from %s (handle %u) to MUCChannel %s (handle %u): %s", G_STRFUNC, from, sender_handle, recipient, handle, body);

			if ((chan = g_hash_table_lookup(priv->muc_channels, GINT_TO_POINTER(handle))) == NULL)
			{
				g_debug("%s: spawning new IdleMUCChannel", G_STRFUNC);

				chan = new_muc_channel(conn, handle, FALSE);
			}

			_idle_muc_channel_receive(chan, msgtype, sender_handle, body);
		}
		else
		{
			g_debug("%s: ignored NOTICE/PRIVMSG from invalid sender (%s)", G_STRFUNC, from);
		}
	}
	else if (g_strncasecmp(cmd, "JOIN", 4) == 0)
	{
		TpHandle handle;
		char *channel = recipient;
		IdleMUCChannel *chan;

		/* I see at least irc.paivola.fi (UnrealIRCD 3.2.4) sending the channel name with a : prefix although it doesn't say that in the RFC */
		if (channel[0] == ':')
		{
			channel++;
		}

		handle = idle_handle_for_room(conn->handles[TP_HANDLE_TYPE_ROOM], channel);

		if (handle == 0)
		{
			g_debug("%s: received JOIN with malformed channel (%s)", G_STRFUNC, channel);
			g_free(channel);
			goto cleanupl;
		}

		chan = g_hash_table_lookup(priv->muc_channels, GINT_TO_POINTER(handle));

		if (chan == NULL)
		{
			g_debug("%s: got JOIN message for channel we don't have in muc_channels? (%s) - creating new", G_STRFUNC, channel);
			chan = new_muc_channel(conn, handle, FALSE);
		}

		g_debug("%s: got JOIN for IRC channel %s (handle %u)", G_STRFUNC, channel, handle);

		_idle_muc_channel_join(chan, from);
	}
	else if (g_strncasecmp(cmd, "NICK", 4) == 0)
	{
		TpHandle old_handle, new_handle;
		gchar *old_down, *new_down;

		old_down = from;
		old_handle = idle_handle_for_contact(conn->handles[TP_HANDLE_TYPE_CONTACT], old_down);

		new_down = recipient+1;
		new_handle = idle_handle_for_contact(conn->handles[TP_HANDLE_TYPE_CONTACT], new_down);

		g_debug("%s: got NICK (%s) -> (%s), %u -> %u", G_STRFUNC, old_down, new_down, old_handle, new_handle);

		priv_rename(conn, old_handle, new_handle);
	}
	else if (g_strncasecmp(cmd, "MODE", 4) == 0)
	{
		TpHandle handle;
		IdleMUCChannel *chan;
		gchar *tmp;
		ucs4char = g_utf8_get_char_validated(recipient, -1);

		if (g_unichar_isalpha(ucs4char))
		{
			g_debug("%s: got user MODE message, ignoring...", G_STRFUNC);
			goto cleanupl;
		}

		handle = idle_handle_for_room(conn->handles[TP_HANDLE_TYPE_ROOM], recipient);

		if (handle == 0)
		{
			g_debug("%s: could not get handle for (%s) in MODE", G_STRFUNC, recipient);
			goto cleanupl;
		}

		chan = g_hash_table_lookup(priv->muc_channels, GINT_TO_POINTER(handle));

		if (chan == NULL)
		{
			g_debug("%s got MODE message for channel we don't have in muc_channels (%s)", G_STRFUNC, recipient);
			goto cleanupl;
		}

		tmp = strstr(msg, recipient);
		g_assert(tmp != NULL);

		tmp = strchr(tmp+1, ' ');
		g_assert(tmp != NULL);

		tmp++;

		g_debug("%s: got MODE for (%s) (%s)", G_STRFUNC, recipient, tmp);

		_idle_muc_channel_mode(chan, tmp);
	}
	else if (g_strncasecmp(cmd, "PART", 4) == 0)
	{
		TpHandle handle;
		IdleMUCChannel *chan;
		gchar *chan_down;

		chan_down = recipient;

		handle = idle_handle_for_room(conn->handles[TP_HANDLE_TYPE_ROOM], chan_down);

		if (handle == 0)
		{
			g_debug("%s: could not get handle for (%s) in PART", G_STRFUNC, chan_down);
			goto cleanupl;
		}

		chan = g_hash_table_lookup(priv->muc_channels, GINT_TO_POINTER(handle));

		if (chan == NULL)
		{
			g_debug("%s: got PART message for channel we don't have in muc_channels? (%s)", G_STRFUNC, chan_down);
			goto cleanupl;
		}

		g_debug("%s: got PART for (%s) in (%s)", G_STRFUNC, from, chan_down);

		_idle_muc_channel_part(chan, from);
	}
	else if (g_strncasecmp(cmd, "KICK", 4) == 0)
	{
		TpHandle handle;
		IdleMUCChannel *chan;
		gchar *chan_down;
		gchar *nick;
		gchar *tmp;
		int i;

		chan_down = recipient;

		handle = idle_handle_for_room(conn->handles[TP_HANDLE_TYPE_ROOM], chan_down);

		if (handle == 0)
		{
			g_debug("%s: could not get handle for (%s) in KICK/PART", G_STRFUNC, chan_down);
			goto cleanupl;
		}

		chan = g_hash_table_lookup(priv->muc_channels, GINT_TO_POINTER(handle));

		if (chan == NULL)
		{
			g_debug("%s: got KICK/PART message for channel we don't have in muc_channels? (%s)", G_STRFUNC, chan_down);
			goto cleanupl;
		}

		nick = strchr(msg, ' ');

		for (i=0; i<2; i++)
		{
			if (nick == NULL)
			{
				g_debug("%s: failed to find nick in KICK message (%s)", G_STRFUNC, msg);
				goto cleanupl;
			}

			nick = strchr(nick+1, ' ');
		}

		if (nick == NULL)
		{
			g_debug("%s: 1failed to find nick in KICK message (%s)", G_STRFUNC, msg);
			goto cleanupl;
		}

		nick++;

		tmp = strchr(nick, ' ');

		if (tmp == NULL)
		{
			g_debug("%s: 2failed to find nick in KICK message (%s)", G_STRFUNC, msg);
			goto cleanupl;
		}

		*tmp = '\0';

		tmp = strchr(sender, '!');

		if (tmp)
		{
			*tmp = '\0';
		}

		g_debug("%s: got KICK for (%s) by (%s) in (%s)", G_STRFUNC, nick, sender, chan_down);

		_idle_muc_channel_kick(chan, nick, sender, TP_CHANNEL_GROUP_CHANGE_REASON_KICKED);
	}
	else if (g_strncasecmp(cmd, "QUIT", 4) == 0)
	{
		TpHandle handle;

		handle = idle_handle_for_contact(conn->handles[TP_HANDLE_TYPE_CONTACT], from);

		if (handle == 0)
		{
			g_debug("%s: could not get handle for (%s) in QUIT", G_STRFUNC, from);
			goto cleanupl;
		}

		g_hash_table_foreach(priv->muc_channels, muc_channel_handle_quit_foreach, GINT_TO_POINTER(handle));
	}
	else if (g_strncasecmp(cmd, "TOPIC", 5) == 0)
	{
		TpHandle handle, setter_handle;
		IdleMUCChannel *chan;
		char *tmp;

		handle = idle_handle_for_room(conn->handles[TP_HANDLE_TYPE_ROOM], recipient);

		if (handle == 0)
		{
			g_debug("%s: could not get handle for (%s) in TOPIC", G_STRFUNC, recipient);
			goto cleanupl;
		}

		chan = g_hash_table_lookup(priv->muc_channels, GINT_TO_POINTER(handle));

		if (chan == NULL)
		{
			g_debug("%s: got NULL MUCChannel for (%s) (handle %u)", G_STRFUNC, recipient, handle);
			goto cleanupl;
		}

		tmp = strstr(msg, " :")+1;

		if (tmp == NULL)
		{
			g_debug("%s: could not find body identifier in TOPIC message (%s)", G_STRFUNC, msg);
			goto cleanupl;
		}

		setter_handle = idle_handle_for_contact(conn->handles[TP_HANDLE_TYPE_CONTACT], from);

		if (setter_handle == 0)
		{
			g_debug("%s: could not get handle for (%s) in TOPIC", G_STRFUNC, from);
			goto cleanupl;
		}

		if (tmp+1 != '\0')
		{
			_idle_muc_channel_topic_full(chan, setter_handle, time(NULL), tmp+1);
		}
		else
		{
			_idle_muc_channel_topic_unset(chan);
		}

		g_debug("%s: got TOPIC for (%s)", G_STRFUNC, recipient);
	}
	else
	{
		g_debug("%s: ignored unparsed message from server (%s) = (%s, %s, %s)", G_STRFUNC, msg, sender, cmd, recipient);
	}

cleanupl:

	if (from != sender)
	{
		g_free(from);
	}

	g_strfreev(tokens);

	return reply;
}

#define IRC_RPL_WELCOME 001
#define IRC_RPL_AWAY 301
#define IRC_RPL_UNAWAY 305
#define IRC_RPL_NOWAWAY 306
#define IRC_RPL_WHOISIDLE 317
#define IRC_RPL_ENDOFWHOIS 318
#define IRC_RPL_MODEREPLY 324
#define IRC_RPL_TOPIC 332
#define IRC_RPL_TOPIC_STAMP 333
#define IRC_RPL_NAMEREPLY 353
#define IRC_ERR_NOSUCHNICK 401
#define IRC_ERR_CANNOTSENDTOCHAN 404
#define IRC_ERR_ERRONEOUSNICKNAME 432
#define IRC_ERR_NICKNAMEINUSE 433
#define IRC_ERR_CHANNELISFULL 471
#define IRC_ERR_INVITEONLYCHAN 473
#define IRC_ERR_BANNEDFROMCHAN 474
#define IRC_ERR_BADCHANNELKEY 475

static gchar *prefix_numeric_parse(IdleConnection *conn, const gchar *msg)
{
	IdleConnectionPrivate *priv;

	gchar *reply = NULL;

	gchar **tokens;
	guint numeric;
	gchar *sender, *recipient;
	int tokenc;

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	tokens = g_strsplit_set(msg, " ", -1);

	for (tokenc = 0; tokens[tokenc] != NULL; tokenc++) {}

	if (tokenc < 3)
	{
		g_debug("%s: got message with less than 3 tokens (%s), ignoring...", G_STRFUNC, msg);

		goto cleanupl;
	}

	sender = tokens[0];
	numeric = atoi(tokens[1]);
	recipient = tokens[2];

	if (!sender || !recipient)
	{
		goto cleanupl;
	}

	else if (numeric == IRC_RPL_TOPIC)
	{
		TpHandle handle;
		IdleMUCChannel *chan;
		gchar *tmp;

		if (tokenc < 5)
		{
			g_debug("%s: not enough tokens for RPL_TOPIC in (%s), ignoring...", G_STRFUNC, msg);
			goto cleanupl;
		}

		handle = idle_handle_for_room(conn->handles[TP_HANDLE_TYPE_ROOM], tokens[3]);

		if (handle == 0)
		{
			g_debug("%s: failed to get handle for (%s) in RPL_TOPIC", G_STRFUNC, tokens[3]);
			goto cleanupl;
		}

		chan = g_hash_table_lookup(priv->muc_channels, GINT_TO_POINTER(handle));

		if (chan == NULL)
		{
			g_debug("%s: failed to find channel (%s) (handle %u) in RPL_TOPIC", G_STRFUNC, tokens[3], handle);
			goto cleanupl;
		}

		tmp = strstr(msg, " :")+1;

		if (tmp == NULL)
		{
			g_debug("%s: failed to find body separator in (%s)", G_STRFUNC, msg);

			goto cleanupl;
		}

		_idle_muc_channel_topic(chan, tmp+1);

		g_debug("%s: got RPL_TOPIC for (%s)", G_STRFUNC, tokens[3]);
	}
	else if (numeric == IRC_RPL_TOPIC_STAMP)
	{
		TpHandle handle, toucher_handle;
		IdleMUCChannel *chan;
		guint timestamp;

		if (tokenc != 6)
		{
			g_debug("%s: wrong amount of tokens (%u) for RPL_TOPICSTAMP in (%s), ignoring...", G_STRFUNC, tokenc, msg);
			goto cleanupl;
		}

		handle = idle_handle_for_room(conn->handles[TP_HANDLE_TYPE_ROOM], tokens[3]);

		if (handle == 0)
		{
			g_debug("%s: failed to get handle for (%s) in RPL_TOPICSTAMP", G_STRFUNC, tokens[3]);
			goto cleanupl;
		}

		chan = g_hash_table_lookup(priv->muc_channels, GINT_TO_POINTER(handle));

		if (chan == NULL)
		{
			g_debug("%s: failed to find channel (%s) (handle %u) in RPL_TOPICSTAMP", G_STRFUNC, tokens[3], handle);
			goto cleanupl;
		}

		toucher_handle = idle_handle_for_contact(conn->handles[TP_HANDLE_TYPE_CONTACT], tokens[4]);

		if (toucher_handle == 0)
		{
			g_debug("%s: failed to get handle for toucher (%s) in RPL_TOPICSTAMP", G_STRFUNC, tokens[4]);
			goto cleanupl;
		}

		if (sscanf(tokens[5], "%u", &timestamp) != 1)
		{
			g_debug("%s: failed to parse (%s) to uint in RPL_TOPICSTAMP", G_STRFUNC, tokens[5]);
			goto cleanupl;
		}

		_idle_muc_channel_topic_touch(chan, toucher_handle, timestamp);

		g_debug("%s: got RPL_TOPICSTAMP for (%s)", G_STRFUNC, tokens[3]);
	}
	else if (numeric == IRC_RPL_MODEREPLY)
	{
		TpHandle handle;
		IdleMUCChannel *chan;
		gchar *tmp;

		if (tokenc < 5)
		{
			g_debug("%s: got IRC_RPL_MODEREPLY with less than 5 tokens, ignoring... (%s)", G_STRFUNC, msg);
			goto cleanupl;
		}

		handle = idle_handle_for_room(conn->handles[TP_HANDLE_TYPE_ROOM], tokens[3]);

		if (handle == 0)
		{
			g_debug("%s: could not get handle for (%s) in MODE", G_STRFUNC, tokens[3]);
			goto cleanupl;
		}

		chan = g_hash_table_lookup(priv->muc_channels, GINT_TO_POINTER(handle));

		if (chan == NULL)
		{
			g_debug("%s got MODE message for channel we don't have in muc_channels (%s)", G_STRFUNC, tokens[3]);
			goto cleanupl;
		}

		tmp = strstr(msg, tokens[3]);
		g_assert(tmp != NULL);

		tmp = strchr(tmp+1, ' ');
		g_assert(tmp != NULL);

		tmp++;

		g_debug("%s: got RPL_MODEREPLY for (%s) (%s)", G_STRFUNC, tokens[3], tmp);

		_idle_muc_channel_mode(chan, tmp);
	}
	else
	{
		g_debug("%s: ignored unparsed message from server (%s)", G_STRFUNC, msg);
	}

cleanupl:

	g_strfreev(tokens);

	return reply;
}
#endif


#if 0
static void connection_disconnect(IdleConnection *conn, TpConnectionStatusReason reason) {
	IdleConnectionPrivate *priv;
	GError *error;

	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	priv->disconnect_reason = reason;

	send_quit_request(conn);

	connection_status_change(conn, TP_CONNECTION_STATUS_DISCONNECTED, reason);

	if (priv->conn != NULL)
	{
		if (!idle_server_connection_iface_disconnect(priv->conn, &error))
		{
			g_debug("%s: server connection failed to disconnect: %s", G_STRFUNC, error->message);
		}
	}
}
#endif

#if 0
static void muc_channel_join_ready_cb(IdleMUCChannel *chan, guint err, gpointer user_data)
{
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	DBusGMethodInvocation *ctx;
	gboolean suppress;
	GError *error;
	gchar *obj_path;
	guint handle;
	chan_req_data *data = g_hash_table_lookup(priv->chan_req_ctxs, chan);

	g_assert(data != NULL);

	ctx = data->ctx;
	suppress = data->suppress_handler;

	g_object_get(chan, "handle", &handle, NULL);
	g_object_get(chan, "object-path", &obj_path, NULL);

	switch (err)
	{
		case MUC_CHANNEL_JOIN_ERROR_NONE:
			{
				dbus_g_method_return(ctx, obj_path);
				g_signal_emit(conn, signals[NEW_CHANNEL], 0, obj_path, TP_IFACE_CHANNEL_TYPE_TEXT,
						TP_HANDLE_TYPE_ROOM, handle, suppress);
			}
			break;
		case MUC_CHANNEL_JOIN_ERROR_BANNED:
			{
				error = g_error_new(TP_ERRORS, TP_ERROR_CHANNEL_BANNED, "banned from room");
				dbus_g_method_return_error(ctx, error);
				g_error_free(error);
			}
			break;
		case MUC_CHANNEL_JOIN_ERROR_FULL:
			{
				error = g_error_new(TP_ERRORS, TP_ERROR_CHANNEL_FULL, "room full");
				dbus_g_method_return_error(ctx, error);
				g_error_free(error);
			}
			break;
		case MUC_CHANNEL_JOIN_ERROR_INVITE_ONLY:
			{
				error = g_error_new(TP_ERRORS, TP_ERROR_CHANNEL_INVITE_ONLY, "room invite only");
				dbus_g_method_return_error(ctx, error);
				g_error_free(error);
			}
			break;
		default:
			g_assert_not_reached();
	}

	if (err != MUC_CHANNEL_JOIN_ERROR_NONE)
	{
		g_hash_table_remove(priv->muc_channels, GINT_TO_POINTER(handle));
	}

	g_hash_table_remove(priv->chan_req_ctxs, chan);
	g_free(obj_path);
}
#endif

