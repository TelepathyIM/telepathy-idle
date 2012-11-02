/*
 * This file is part of telepathy-idle
 *
 * Copyright (C) 2006-2007 Collabora Limited
 * Copyright (C) 2006-2007 Nokia Corporation
 * Copyright (C) 2011      Debarshi Ray <rishi@gnu.org>
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

#include "idle-connection.h"

#include <string.h>
#include <time.h>

#include <dbus/dbus-glib.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/simple-password-manager.h>
#include <telepathy-glib/svc-connection.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_CONNECTION
#include "idle-contact-info.h"
#include "idle-ctcp.h"
#include "idle-debug.h"
#include "idle-handles.h"
#include "idle-im-manager.h"
#include "idle-muc-manager.h"
#include "idle-roomlist-manager.h"
#include "idle-parser.h"
#include "idle-server-connection.h"

#include "extensions/extensions.h"    /* Renaming */

#define DEFAULT_KEEPALIVE_INTERVAL 30 /* sec */
#define MISSED_KEEPALIVES_BEFORE_DISCONNECTING 3

/* From RFC 2813 :
 * This in essence means that the client may send one (1) message every
 * two (2) seconds without being adversely affected.  Services MAY also
 * be subject to this mechanism.
 */
#define MSG_QUEUE_TIMEOUT 2
static gboolean flush_queue_faster = FALSE;

#define SERVER_CMD_MIN_PRIORITY 0
#define SERVER_CMD_NORMAL_PRIORITY G_MAXUINT/2
#define SERVER_CMD_MAX_PRIORITY G_MAXUINT

static void _free_alias_pair(gpointer data, gpointer user_data)
{
	g_boxed_free(TP_STRUCT_TYPE_ALIAS_PAIR, data);
}

static void _aliasing_iface_init(gpointer, gpointer);
static void _renaming_iface_init(gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(IdleConnection, idle_connection, TP_TYPE_BASE_CONNECTION,
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING, _aliasing_iface_init);
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_INFO, idle_contact_info_iface_init);
		G_IMPLEMENT_INTERFACE(IDLE_TYPE_SVC_CONNECTION_INTERFACE_RENAMING, _renaming_iface_init);
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACTS, tp_contacts_mixin_iface_init);
);

typedef struct _IdleOutputPendingMsg IdleOutputPendingMsg;

struct _IdleOutputPendingMsg {
	gchar *message;
	guint priority;
	guint64 id;
};

/* Steals @message. */
static IdleOutputPendingMsg *
idle_output_pending_msg_new (
    gchar *message,
    guint priority)
{
	IdleOutputPendingMsg *msg = g_slice_new(IdleOutputPendingMsg);
	static guint64 last_id = 0;

	msg->message = message;
	msg->priority = priority;
	msg->id = last_id++;

	return msg;
}

static void idle_output_pending_msg_free(IdleOutputPendingMsg *msg) {
	if (!msg)
		return;

	g_free(msg->message);
	g_slice_free(IdleOutputPendingMsg, msg);
}

static gint pending_msg_compare(gconstpointer a, gconstpointer b, gpointer unused) {
	const IdleOutputPendingMsg *msg1 = a, *msg2 = b;

	if (msg1->priority == msg2->priority) {
		/* prefer the message with the lower id */
		return (msg1->id - msg2->id);
	}

	/* prefer the message with the higher priority */
	return (msg2->priority - msg1->priority);
}

enum {
	PROP_NICKNAME = 1,
	PROP_SERVER,
	PROP_PORT,
	PROP_PASSWORD,
	PROP_REALNAME,
	PROP_USERNAME,
	PROP_CHARSET,
	PROP_KEEPALIVE_INTERVAL,
	PROP_QUITMESSAGE,
	PROP_USE_SSL,
	PROP_PASSWORD_PROMPT,
	LAST_PROPERTY_ENUM
};

struct _IdleConnectionPrivate {
	/*
	 * network connection
	 */
	IdleServerConnection *conn;
	guint sconn_status;

	/* When we sent a PING to the server which it hasn't PONGed for yet, or 0 if
	 * there isn't a PING outstanding.
	 */
	gint64 ping_time;

	/* IRC connection properties */
	char *nickname;
	char *server;
	guint port;
	char *password;
	char *realname;
	char *username;
	char *charset;
	guint keepalive_interval;
	char *quit_message;
	gboolean use_ssl;
	gboolean password_prompt;

	/* the string used by the a server as a prefix to any messages we send that
	 * it relays to other users.  We need to know this so we can keep our sent
	 * messages short enough that they still fit in the 512-byte limit even with
	 * this prefix added */
	char *relay_prefix;

	/* output message queue */
	GQueue *msg_queue;

	/* has it submitted a message for sending and waiting for acknowledgement */
	gboolean msg_sending;

	/* UNIX time the last message was sent on */
	time_t last_msg_sent;

	/* GSource id for keep alive message timeout */
	guint keepalive_timeout;

	/* GSource id for message queue unloading timeout */
	guint msg_queue_timeout;

	/* if we are quitting asynchronously */
	gboolean quitting;
	guint force_disconnect_id;

	/* AliasChanged aggregation */
	GPtrArray *queued_aliases;
	TpHandleSet *queued_aliases_owners;

	/* if idle_connection_dispose has already run once */
	gboolean dispose_has_run;

	/* so we can pop up a SASL channel asking for the password */
	TpSimplePasswordManager *password_manager;
};

static void _iface_create_handle_repos(TpBaseConnection *self, TpHandleRepoIface **repos);
static GPtrArray *_iface_create_channel_managers(TpBaseConnection *self);
static gchar *_iface_get_unique_connection_name(TpBaseConnection *self);
static void _iface_disconnected(TpBaseConnection *self);
static void _iface_shut_down(TpBaseConnection *self);
static gboolean _iface_start_connecting(TpBaseConnection *self, GError **error);

static IdleParserHandlerResult _error_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _erroneous_nickname_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _nick_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _nickname_in_use_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _ping_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _pong_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _unknown_command_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _version_privmsg_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _welcome_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);
static IdleParserHandlerResult _whois_user_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);

static void sconn_status_changed_cb(IdleServerConnection *sconn, IdleServerConnectionState state, IdleServerConnectionStateReason reason, IdleConnection *conn);
static void sconn_received_cb(IdleServerConnection *sconn, gchar *raw_msg, IdleConnection *conn);

static void irc_handshakes(IdleConnection *conn);
static void send_quit_request(IdleConnection *conn);
static void connection_connect_cb(IdleConnection *conn, gboolean success, TpConnectionStatusReason fail_reason);
static void connection_disconnect_cb(IdleConnection *conn, TpConnectionStatusReason reason);
static gboolean idle_connection_hton(IdleConnection *obj, const gchar *input, gchar **output, GError **_error);
static gchar *idle_connection_ntoh(IdleConnection *obj, const gchar *input);

static void idle_connection_add_queue_timeout (IdleConnection *self);
static void idle_connection_clear_queue_timeout (IdleConnection *self);

static void _send_with_priority(IdleConnection *conn, const gchar *msg, guint priority);
static void conn_aliasing_fill_contact_attributes (
    GObject *obj,
    const GArray *contacts,
    GHashTable *attributes_hash);

static void idle_connection_init(IdleConnection *obj) {
	IdleConnectionPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj, IDLE_TYPE_CONNECTION, IdleConnectionPrivate);

	obj->priv = priv;
	priv->sconn_status = SERVER_CONNECTION_STATE_NOT_CONNECTED;
	priv->msg_queue = g_queue_new();

	tp_contacts_mixin_init ((GObject *) obj, G_STRUCT_OFFSET (IdleConnection, contacts));
	tp_base_connection_register_with_contacts_mixin ((TpBaseConnection *) obj);
}

static void
idle_connection_constructed (GObject *object)
{
  IdleConnection *self = IDLE_CONNECTION (object);

  self->parser = g_object_new (IDLE_TYPE_PARSER, "connection", self, NULL);
  idle_contact_info_init (self);
  tp_contacts_mixin_add_contact_attributes_iface (object,
      TP_IFACE_CONNECTION_INTERFACE_ALIASING,
      conn_aliasing_fill_contact_attributes);
}

static void idle_connection_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec) {
	IdleConnection *self = IDLE_CONNECTION (obj);
	IdleConnectionPrivate *priv = self->priv;

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

		case PROP_USERNAME:
			g_free(priv->username);
			priv->username = g_value_dup_string(value);
			break;

		case PROP_CHARSET:
			g_free(priv->charset);
			priv->charset = g_value_dup_string(value);
			break;

		case PROP_KEEPALIVE_INTERVAL:
			priv->keepalive_interval = g_value_get_uint(value);
			break;

		case PROP_QUITMESSAGE:
			g_free(priv->quit_message);
			priv->quit_message = g_value_dup_string(value);
			break;

		case PROP_USE_SSL:
			priv->use_ssl = g_value_get_boolean(value);
			break;

		case PROP_PASSWORD_PROMPT:
			priv->password_prompt = g_value_get_boolean(value);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
			break;
	}
}

static void idle_connection_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec) {
	IdleConnection *self = IDLE_CONNECTION (obj);
	IdleConnectionPrivate *priv = self->priv;

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

		case PROP_USERNAME:
			g_value_set_string(value, priv->username);
			break;

		case PROP_CHARSET:
			g_value_set_string(value, priv->charset);
			break;

		case PROP_KEEPALIVE_INTERVAL:
			g_value_set_uint(value, priv->keepalive_interval);
			break;

		case PROP_QUITMESSAGE:
			g_value_set_string(value, priv->quit_message);
			break;

		case PROP_USE_SSL:
			g_value_set_boolean(value, priv->use_ssl);
			break;

		case PROP_PASSWORD_PROMPT:
			g_value_set_boolean(value, priv->password_prompt);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
			break;
	}
}

static void idle_connection_dispose (GObject *object) {
	IdleConnection *self = IDLE_CONNECTION(object);
	IdleConnectionPrivate *priv = self->priv;

	if (priv->dispose_has_run)
		return;

	priv->dispose_has_run = TRUE;

	if (priv->keepalive_timeout) {
		g_source_remove(priv->keepalive_timeout);
		priv->keepalive_timeout = 0;
	}

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
	IdleConnection *self = IDLE_CONNECTION (object);
	IdleConnectionPrivate *priv = self->priv;
	IdleOutputPendingMsg *msg;

	idle_contact_info_finalize(object);

	g_free(priv->nickname);
	g_free(priv->server);
	g_free(priv->password);
	g_free(priv->realname);
	g_free(priv->username);
	g_free(priv->charset);
	g_free(priv->relay_prefix);
	g_free(priv->quit_message);

	while ((msg = g_queue_pop_head(priv->msg_queue)) != NULL)
		idle_output_pending_msg_free(msg);

	g_queue_free(priv->msg_queue);
	tp_contacts_mixin_finalize (object);

	G_OBJECT_CLASS(idle_connection_parent_class)->finalize(object);
}

static const gchar * interfaces_always_present[] = {
	TP_IFACE_CONNECTION_INTERFACE_ALIASING,
	TP_IFACE_CONNECTION_INTERFACE_CONTACT_INFO,
	IDLE_IFACE_CONNECTION_INTERFACE_RENAMING,
	TP_IFACE_CONNECTION_INTERFACE_REQUESTS,
	TP_IFACE_CONNECTION_INTERFACE_CONTACTS,
	NULL};

const gchar * const *idle_connection_get_implemented_interfaces (void) {
	/* we don't have any conditionally-implemented interfaces yet */
	return interfaces_always_present;
}

static void idle_connection_class_init(IdleConnectionClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	TpBaseConnectionClass *parent_class = TP_BASE_CONNECTION_CLASS(klass);
	GParamSpec *param_spec;

	g_type_class_add_private(klass, sizeof(IdleConnectionPrivate));

	object_class->constructed = idle_connection_constructed;
	object_class->set_property = idle_connection_set_property;
	object_class->get_property = idle_connection_get_property;
	object_class->dispose = idle_connection_dispose;
	object_class->finalize = idle_connection_finalize;

	parent_class->create_handle_repos = _iface_create_handle_repos;
	parent_class->get_unique_connection_name = _iface_get_unique_connection_name;
	parent_class->create_channel_factories = NULL;
	parent_class->create_channel_managers = _iface_create_channel_managers;
	parent_class->connecting = NULL;
	parent_class->connected = NULL;
	parent_class->disconnected = _iface_disconnected;
	parent_class->shut_down = _iface_shut_down;
	parent_class->start_connecting = _iface_start_connecting;
	parent_class->interfaces_always_present = interfaces_always_present;

	param_spec = g_param_spec_string("nickname", "IRC nickname", "The nickname to be visible to others in IRC.", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property(object_class, PROP_NICKNAME, param_spec);

	param_spec = g_param_spec_string("server", "Hostname or IP of the IRC server to connect to", "The server used when establishing the connection.", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property(object_class, PROP_SERVER, param_spec);

	param_spec = g_param_spec_uint("port", "IRC server port", "The destination port used when establishing the connection.", 0, G_MAXUINT16, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
	g_object_class_install_property(object_class, PROP_PORT, param_spec);

	param_spec = g_param_spec_string("password", "Server password", "Password to authenticate to the server with", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property(object_class, PROP_PASSWORD, param_spec);

	param_spec = g_param_spec_string("realname", "Real name", "The real name of the user connecting to IRC", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property(object_class, PROP_REALNAME, param_spec);

	param_spec = g_param_spec_string("username", "User name", "The username of the user connecting to IRC", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property(object_class, PROP_USERNAME, param_spec);

	param_spec = g_param_spec_string("charset", "Character set", "The character set to use to communicate with the outside world", "NULL", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
	g_object_class_install_property(object_class, PROP_CHARSET, param_spec);

	param_spec = g_param_spec_uint("keepalive-interval", "Keepalive interval", "Seconds between keepalive packets, or 0 to disable", 0, G_MAXUINT, DEFAULT_KEEPALIVE_INTERVAL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
	g_object_class_install_property(object_class, PROP_KEEPALIVE_INTERVAL, param_spec);

	param_spec = g_param_spec_string("quit-message", "Quit message", "The quit message to send to the server when leaving IRC", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
	g_object_class_install_property(object_class, PROP_QUITMESSAGE, param_spec);

	param_spec = g_param_spec_boolean("use-ssl", "Use SSL", "If the connection should use a SSL tunneled socket connection", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
	g_object_class_install_property(object_class, PROP_USE_SSL, param_spec);

	param_spec = g_param_spec_boolean("password-prompt", "Password prompt", "Whether the connection should pop up a SASL channel if no password is given", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
	g_object_class_install_property(object_class, PROP_PASSWORD_PROMPT, param_spec);

	tp_contacts_mixin_class_init (object_class, G_STRUCT_OFFSET (IdleConnectionClass, contacts));
	idle_contact_info_class_init(klass);

	/* This is a hack to make the test suite run in finite time. */
	if (!tp_str_empty (g_getenv ("IDLE_HTFU")))
		flush_queue_faster = TRUE;
}

static GPtrArray *_iface_create_channel_managers(TpBaseConnection *base) {
	IdleConnection *self = IDLE_CONNECTION (base);
	IdleConnectionPrivate *priv = self->priv;
	GPtrArray *managers = g_ptr_array_sized_new(1);
	GObject *manager;

	manager = g_object_new(IDLE_TYPE_IM_MANAGER, "connection", self, NULL);
	g_ptr_array_add(managers, manager);

	manager = g_object_new(IDLE_TYPE_MUC_MANAGER, "connection", self, NULL);
	g_ptr_array_add(managers, manager);

	priv->password_manager = tp_simple_password_manager_new(base);
	g_ptr_array_add(managers, priv->password_manager);

	manager = g_object_new(IDLE_TYPE_ROOMLIST_MANAGER, "connection", self, NULL);
	g_ptr_array_add(managers, manager);

	return managers;
}

static void _iface_create_handle_repos(TpBaseConnection *self, TpHandleRepoIface **repos) {
	for (int i = 0; i < NUM_TP_HANDLE_TYPES; i++)
		repos[i] = NULL;

	idle_handle_repos_init(repos);
}

static gchar *_iface_get_unique_connection_name(TpBaseConnection *base) {
	IdleConnection *self = IDLE_CONNECTION (base);
	IdleConnectionPrivate *priv = self->priv;

	return g_strdup_printf("%s@%s%p", priv->nickname, priv->server, self);
}

static gboolean _finish_shutdown_idle_func(gpointer data) {
	TpBaseConnection *conn = TP_BASE_CONNECTION(data);
	IdleConnection *self = IDLE_CONNECTION(conn);
	IdleConnectionPrivate *priv = self->priv;
	if (priv->force_disconnect_id != 0) {
		g_source_remove(priv->force_disconnect_id);
	}

	tp_base_connection_finish_shutdown(conn);

	return FALSE;
}

static gboolean
_force_disconnect (gpointer data)
{
	IdleConnection *conn = IDLE_CONNECTION(data);
	IdleConnectionPrivate *priv = conn->priv;

	IDLE_DEBUG("gave up waiting, forcibly disconnecting");
	idle_server_connection_force_disconnect(priv->conn);
	return FALSE;
}

static void _iface_disconnected(TpBaseConnection *self) {
	IdleConnection *conn = IDLE_CONNECTION(self);
	IdleConnectionPrivate *priv = conn->priv;

	/* we never got around to actually creating the connection
	 * iface object because we were still trying to connect, so
	 * don't try to send any traffic down it */
	if (priv->conn == NULL) {
		return;
	}

	send_quit_request(conn);

	priv->quitting = TRUE;
	/* don't handle any more messages, we're quitting.  See e.g. bug #19762 */
	idle_parser_remove_handlers_by_data(conn->parser, conn);
	/* schedule forceful disconnect for 2 seconds if the remote server doesn't
	 * respond or disconnect before then */
	priv->force_disconnect_id = g_timeout_add_seconds(2, _force_disconnect, conn);
}

static void _iface_shut_down(TpBaseConnection *base) {
	IdleConnection *self = IDLE_CONNECTION (base);
	IdleConnectionPrivate *priv = self->priv;

	if (priv->quitting)
		return;

	/* we never got around to actually creating the connection
	 * iface object because we were still trying to connect, so
	 * don't try to send any traffic down it */
	if (priv->conn == NULL) {
		g_idle_add(_finish_shutdown_idle_func, self);
	} else {
		idle_server_connection_disconnect_async(priv->conn, NULL, NULL, NULL);
	}
}

static void _connection_disconnect_with_gerror(IdleConnection *conn, TpConnectionStatusReason reason, const gchar *key, const GError *error) {
	if (TP_BASE_CONNECTION (conn)->status == TP_CONNECTION_STATUS_DISCONNECTED) {
		IDLE_DEBUG ("Already disconnected; refusing to report error %s", error->message);
	} else {
		GHashTable *details = tp_asv_new(key, G_TYPE_STRING, error->message, NULL);

		g_assert(error->domain == TP_ERROR);

		tp_base_connection_disconnect_with_dbus_error(TP_BASE_CONNECTION(conn),
							      tp_error_get_dbus_name(error->code),
							      details,
							      reason);
		g_hash_table_unref(details);
	}
}

static void _connection_disconnect_with_error(IdleConnection *conn, TpConnectionStatusReason reason, const gchar *key, GQuark domain, gint code, const gchar *message) {
	GError *error = g_error_new_literal(domain, code, message);

	_connection_disconnect_with_gerror(conn, reason, key, error);
	g_error_free(error);
}

static void _start_connecting_continue(IdleConnection *conn);

static void _password_prompt_cb(GObject *source, GAsyncResult *result, gpointer user_data) {
	IdleConnection *conn = user_data;
	TpBaseConnection *base_conn = TP_BASE_CONNECTION(conn);
	IdleConnectionPrivate *priv = conn->priv;
	const GString *password;
	GError *error = NULL;

	password = tp_simple_password_manager_prompt_finish(TP_SIMPLE_PASSWORD_MANAGER(source), result, &error);

	if (error != NULL) {
		IDLE_DEBUG("Simple password manager failed: %s", error->message);

		if (base_conn->status != TP_CONNECTION_STATUS_DISCONNECTED)
			_connection_disconnect_with_gerror(conn,
							   TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED,
							   "debug-message",
							   error);

		g_error_free(error);
		return;
	}

	g_free(priv->password);
	priv->password = g_strdup(password->str);

	_start_connecting_continue(conn);
}

static gboolean _iface_start_connecting(TpBaseConnection *self, GError **error) {
	IdleConnection *conn = IDLE_CONNECTION(self);
	IdleConnectionPrivate *priv = conn->priv;

	g_assert(priv->nickname != NULL);
	g_assert(priv->server != NULL);
	g_assert(priv->port > 0 && priv->port <= G_MAXUINT16);

	if (priv->conn != NULL) {
		IDLE_DEBUG("conn already open!");
		g_set_error(error, TP_ERROR, TP_ERROR_NOT_AVAILABLE, "connection already open!");
		return FALSE;
	}

	if (priv->password_prompt) {
		tp_simple_password_manager_prompt_async(priv->password_manager, _password_prompt_cb, conn);
	} else {
		_start_connecting_continue(conn);
	}

	return TRUE;
}

static void _connection_connect_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
	IdleServerConnection *sconn = IDLE_SERVER_CONNECTION(source_object);
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	IdleConnectionPrivate *priv = conn->priv;
	GError *error = NULL;

	if (!idle_server_connection_connect_finish(sconn, res, &error)) {
		IDLE_DEBUG("idle_server_connection_connect failed: %s", error->message);
		_connection_disconnect_with_gerror(conn, TP_CONNECTION_STATUS_REASON_NETWORK_ERROR, "debug-message", error);
		g_error_free(error);
		g_object_unref(sconn);
		return;
	}

	priv->conn = sconn;

	g_signal_connect(sconn, "received", (GCallback)(sconn_received_cb), conn);

	idle_parser_add_handler(conn->parser, IDLE_PARSER_CMD_ERROR, _error_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_ERRONEOUSNICKNAME, _erroneous_nickname_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_NICKNAMEINUSE, _nickname_in_use_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_WELCOME, _welcome_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_WHOISUSER, _whois_user_handler, conn);

	idle_parser_add_handler(conn->parser, IDLE_PARSER_CMD_PING, _ping_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_PREFIXCMD_PONG, _pong_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_UNKNOWNCOMMAND, _unknown_command_handler, conn);

	idle_parser_add_handler_with_priority(conn->parser, IDLE_PARSER_PREFIXCMD_NICK, _nick_handler, conn, IDLE_PARSER_HANDLER_PRIORITY_FIRST);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_PREFIXCMD_PRIVMSG_USER, _version_privmsg_handler, conn);

	irc_handshakes(conn);
}

static void _start_connecting_continue(IdleConnection *conn) {
	IdleConnectionPrivate *priv = conn->priv;
	IdleServerConnection *sconn;

	if (tp_str_empty(priv->realname)) {
		const gchar *g_realname = g_get_real_name();

		g_free(priv->realname);

		if (tp_strdiff(g_realname, "Unknown"))
			priv->realname = g_strdup(g_realname);
		else
			priv->realname = g_strdup(priv->nickname);
	}

	if (tp_str_empty(priv->username)) {
		g_free(priv->username);
		priv->username = g_strdup(g_get_user_name());
	}

	sconn = g_object_new(IDLE_TYPE_SERVER_CONNECTION, "host", priv->server, "port", priv->port, NULL);
	if (priv->use_ssl)
		idle_server_connection_set_tls(sconn, TRUE);

	g_signal_connect(sconn, "status-changed", (GCallback)(sconn_status_changed_cb), conn);

	idle_server_connection_connect_async(sconn, NULL, _connection_connect_ready, conn);
}

static gboolean keepalive_timeout_cb(gpointer user_data);

static void sconn_status_changed_cb(IdleServerConnection *sconn, IdleServerConnectionState state, IdleServerConnectionStateReason reason, IdleConnection *conn) {
	IdleConnectionPrivate *priv = conn->priv;
	TpConnectionStatusReason tp_reason;

	/* cancel scheduled forced disconnect since we are now disconnected */
	if (state == SERVER_CONNECTION_STATE_NOT_CONNECTED &&
		priv->force_disconnect_id) {
		g_source_remove(priv->force_disconnect_id);
		priv->force_disconnect_id = 0;
	}

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
			connection_disconnect_cb(conn, tp_reason);
			break;

		case SERVER_CONNECTION_STATE_CONNECTING:
			break;

		case SERVER_CONNECTION_STATE_CONNECTED:
			if (priv->keepalive_interval != 0 && priv->keepalive_timeout == 0)
				priv->keepalive_timeout = g_timeout_add_seconds(priv->keepalive_interval, keepalive_timeout_cb, conn);

			if (g_queue_get_length(priv->msg_queue) > 0) {
				IDLE_DEBUG("we had messages in queue, start unloading them now");
				idle_connection_add_queue_timeout (conn);
			}
			break;

		default:
			g_assert_not_reached();
			break;
	}

	priv->sconn_status = state;
}

static void sconn_received_cb(IdleServerConnection *sconn, gchar *raw_msg, IdleConnection *conn) {
	gchar *converted = idle_connection_ntoh(conn, raw_msg);
	idle_parser_receive(conn->parser, converted);

	g_free(converted);
}

static gboolean keepalive_timeout_cb(gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	IdleConnectionPrivate *priv = conn->priv;
	gchar cmd[IRC_MSG_MAXLEN + 1];
	gint64 now;

	if (priv->sconn_status != SERVER_CONNECTION_STATE_CONNECTED ||
	    priv->quitting) {
		priv->keepalive_timeout = 0;
		return FALSE;
	}

	now = g_get_real_time();

	if (priv->ping_time != 0) {
		gint64 seconds_since_ping = (now - priv->ping_time) / G_USEC_PER_SEC;
		gint64 grace_period = priv->keepalive_interval * MISSED_KEEPALIVES_BEFORE_DISCONNECTING;

		if (seconds_since_ping > grace_period) {
			IDLE_DEBUG("haven't heard from the server in %" G_GINT64_FORMAT " seconds "
				"(more than %u keepalive intervals)",
				seconds_since_ping, MISSED_KEEPALIVES_BEFORE_DISCONNECTING);

			idle_server_connection_force_disconnect(priv->conn);
			return FALSE;
		}

		return TRUE;
	}

	if (priv->msg_queue->length > 0) {
		/* No point in sending a PING if we're sending data anyway. */
		return TRUE;
	}

	priv->ping_time = now;
	g_snprintf(cmd, IRC_MSG_MAXLEN + 1, "PING %" G_GINT64_FORMAT, priv->ping_time);
	_send_with_priority(conn, cmd, SERVER_CMD_MIN_PRIORITY);

	return TRUE;
}

static void _msg_queue_timeout_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
	IdleServerConnection *sconn = IDLE_SERVER_CONNECTION(source_object);
	IdleConnection *conn = IDLE_CONNECTION (user_data);
	IdleConnectionPrivate *priv = conn->priv;
	GError *error = NULL;

	priv->msg_sending = FALSE;

	if (!idle_server_connection_send_finish(sconn, res, &error)) {
		IDLE_DEBUG("idle_server_connection_send failed: %s", error->message);
		g_error_free(error);
		return;
	}

	priv->last_msg_sent = time(NULL);
}

static gboolean msg_queue_timeout_cb(gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	IdleConnectionPrivate *priv = conn->priv;
	IdleOutputPendingMsg *output_msg;

	IDLE_DEBUG("called");

	if (priv->sconn_status != SERVER_CONNECTION_STATE_CONNECTED) {
		IDLE_DEBUG("connection was not connected!");

		priv->msg_queue_timeout = 0;

		return FALSE;
	}

	if (priv->msg_sending)
		return TRUE;

	output_msg = g_queue_pop_head(priv->msg_queue);

	if (output_msg == NULL) {
		priv->msg_queue_timeout = 0;
		return FALSE;
	}

	priv->msg_sending = TRUE;
	idle_server_connection_send_async(priv->conn, output_msg->message, NULL, _msg_queue_timeout_ready, conn);
	idle_output_pending_msg_free (output_msg);

	return TRUE;
}

static void
idle_connection_add_queue_timeout (IdleConnection *self)
{
  IdleConnectionPrivate *priv = self->priv;

  if (priv->msg_queue_timeout == 0)
    {
      time_t curr_time = time(NULL);

      if (flush_queue_faster)
        priv->msg_queue_timeout = g_timeout_add (MSG_QUEUE_TIMEOUT,
            msg_queue_timeout_cb, self);
      else
        priv->msg_queue_timeout = g_timeout_add_seconds (MSG_QUEUE_TIMEOUT,
            msg_queue_timeout_cb, self);

      /* If it's been long enough since the last message went out, flush one
       * immediately.
       */
      if (curr_time - priv->last_msg_sent > MSG_QUEUE_TIMEOUT)
        msg_queue_timeout_cb (self);
    }
}

static void
idle_connection_clear_queue_timeout (IdleConnection *self)
{
  IdleConnectionPrivate *priv = self->priv;

  if (priv->msg_queue_timeout != 0)
    {
      g_source_remove (priv->msg_queue_timeout);
      priv->msg_queue_timeout = 0;
    }
}

/**
 * Queue a IRC command for sending, clipping it to IRC_MSG_MAXLEN bytes and appending the required <CR><LF> to it
 */
static void _send_with_priority(IdleConnection *conn, const gchar *msg, guint priority) {
	IdleConnectionPrivate *priv = conn->priv;
	gchar cmd[IRC_MSG_MAXLEN + 3];
	int len;
	gchar *converted;
	GError *convert_error = NULL;

	g_assert(msg != NULL);

	/* Clip the message */
	g_strlcpy(cmd, msg, IRC_MSG_MAXLEN + 1);

	/* Strip out any <CR>/<LF> which have crept in */
	g_strdelimit (cmd, "\r\n", ' ');

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

	g_queue_insert_sorted(priv->msg_queue,
		idle_output_pending_msg_new(converted, priority),
		pending_msg_compare, NULL);
	idle_connection_add_queue_timeout (conn);
}

void idle_connection_send(IdleConnection *conn, const gchar *msg) {
	_send_with_priority(conn, msg, SERVER_CMD_NORMAL_PRIORITY);
}

gsize
idle_connection_get_max_message_length(IdleConnection *conn)
{
	IdleConnectionPrivate *priv = conn->priv;
	if (priv->relay_prefix != NULL) {
		/* server will add ':<relay_prefix> ' to all messages it relays on to
		 * other users.  the +2 is for the initial : and the trailing space */
		return IRC_MSG_MAXLEN - (strlen(priv->relay_prefix) + 2);
	}
	/* Before we've gotten our user info, we don't know how long our relay
	 * prefix will be, so just assume worst-case.  The max possible prefix is:
	 * ':<15 char nick>!<? char username>@<63 char hostname> ' == 1 + 15 + 1 + ?
	 * + 1 + 63 + 1 == 82 + ?
	 * I haven't been able to find a definitive reference for the max username
	 * length, but the testing I've done seems to indicate that 8-10 is a
	 * common limit.  I'll add some extra buffer to be safe.
	 * */
	return IRC_MSG_MAXLEN - 100;
}

static IdleParserHandlerResult _error_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	TpConnectionStatus status = conn->parent.status;
	TpConnectionStatusReason reason;
	const gchar *msg;
	const gchar *begin;
	const gchar *end;
	gchar *server_msg = NULL;
	gint error;

	switch (status) {
		case TP_CONNECTION_STATUS_CONNECTING:
			reason = TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED;
			error = TP_ERROR_AUTHENTICATION_FAILED;
			break;

		case TP_CONNECTION_STATUS_CONNECTED:
			reason = TP_CONNECTION_STATUS_REASON_NETWORK_ERROR;
			error = TP_ERROR_NETWORK_ERROR;
			break;

		default:
			return IDLE_PARSER_HANDLER_RESULT_HANDLED;
	}

	msg = g_value_get_string(g_value_array_get_nth(args, 0));
	begin = strchr(msg, '(');
	end = strrchr(msg, ')');

	if (begin != NULL && end != NULL && begin < end - 1) {
		guint length;

		begin++;
		end--;
		length = end - begin + 1;
		server_msg = g_strndup(begin, length);
	}

	_connection_disconnect_with_error(conn, reason, "server-message", TP_ERROR, error, (server_msg == NULL) ? "" : server_msg);
	g_free(server_msg);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
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
		IDLE_DEBUG("Self renamed: handle was %d, now %d", old_handle, new_handle);
		tp_base_connection_set_self_handle(TP_BASE_CONNECTION(conn), new_handle);
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

static IdleParserHandlerResult _pong_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	IdleConnectionPrivate *priv = conn->priv;

	/* We could compare ping_time to g_get_real_time() and give some indication
	 * of lag, if we were feeling enthusiastic.
	 */
	priv->ping_time = 0;

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _unknown_command_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	IdleConnectionPrivate *priv = conn->priv;
	const gchar *command = g_value_get_string(g_value_array_get_nth(args, 0));

	if (!tp_strdiff(command, "PING")) {
		IDLE_DEBUG("PING not supported, disabling keepalive.");
		g_source_remove(priv->keepalive_timeout);
		priv->keepalive_timeout = 0;
		priv->ping_time = 0;

		return IDLE_PARSER_HANDLER_RESULT_HANDLED;
	}

	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _version_privmsg_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	const gchar *msg = g_value_get_string(g_value_array_get_nth(args, 2));
	TpHandle handle;
	const gchar *nick;
	gchar *reply;

	if (g_ascii_strcasecmp(msg, "\001VERSION\001"))
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	nick = tp_handle_inspect(tp_base_connection_get_handles(TP_BASE_CONNECTION(conn), TP_HANDLE_TYPE_CONTACT), handle);
	reply = g_strdup_printf("VERSION telepathy-idle %s Telepathy IM/VoIP Framework http://telepathy.freedesktop.org", VERSION);

	idle_ctcp_notice(nick, reply, conn);

	g_free(reply);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult _welcome_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	TpHandle handle = g_value_get_uint(g_value_array_get_nth(args, 0));

	tp_base_connection_set_self_handle(TP_BASE_CONNECTION(conn), handle);

	connection_connect_cb(conn, TRUE, 0);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static IdleParserHandlerResult
_whois_user_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data)
{
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	IdleConnectionPrivate *priv = conn->priv;

	/* message format: <nick> <user> <host> * :<real name> */
	TpHandle handle = g_value_get_uint(g_value_array_get_nth(args, 0));
	TpHandle self = tp_base_connection_get_self_handle(TP_BASE_CONNECTION(conn));
	if (handle == self) {
			const char *user;
			const char *host;

			if (priv->relay_prefix != NULL) {
					g_free(priv->relay_prefix);
			}

			user = g_value_get_string(g_value_array_get_nth(args, 1));
			host = g_value_get_string(g_value_array_get_nth(args, 2));
			priv->relay_prefix = g_strdup_printf("%s!%s@%s", priv->nickname, user, host);
			IDLE_DEBUG("user host prefix = %s", priv->relay_prefix);
	}
	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static void irc_handshakes(IdleConnection *conn) {
	IdleConnectionPrivate *priv;
	gchar msg[IRC_MSG_MAXLEN + 1];

	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));

	priv = conn->priv;

	if ((priv->password != NULL) && (priv->password[0] != '\0')) {
		g_snprintf(msg, IRC_MSG_MAXLEN + 1, "PASS %s", priv->password);
		_send_with_priority(conn, msg, SERVER_CMD_NORMAL_PRIORITY + 1);
	}

	g_snprintf(msg, IRC_MSG_MAXLEN + 1, "NICK %s", priv->nickname);
	idle_connection_send(conn, msg);

	g_snprintf(msg, IRC_MSG_MAXLEN + 1, "USER %s %u * :%s", priv->username, 8, priv->realname);
	idle_connection_send(conn, msg);

	/* gather some information about ourselves */
	g_snprintf(msg, IRC_MSG_MAXLEN + 1, "WHOIS %s", priv->nickname);
	idle_connection_send(conn, msg);
}

static void send_quit_request(IdleConnection *conn) {
	IdleConnectionPrivate *priv = conn->priv;
	gchar cmd[IRC_MSG_MAXLEN + 1] = "QUIT";

	if (priv->quit_message != NULL)
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

	if (base->status == TP_CONNECTION_STATUS_DISCONNECTED)
		g_idle_add(_finish_shutdown_idle_func, base);
	else
		tp_base_connection_change_status(base, TP_CONNECTION_STATUS_DISCONNECTED, reason);

	idle_connection_clear_queue_timeout (conn);
}

static void
_queue_alias_changed(IdleConnection *conn, TpHandle handle, const gchar *alias) {
	IdleConnectionPrivate *priv = conn->priv;

	if (!priv->queued_aliases_owners) {
		TpHandleRepoIface *handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(conn), TP_HANDLE_TYPE_CONTACT);
		priv->queued_aliases_owners = tp_handle_set_new(handles);
	}

	tp_handle_set_add(priv->queued_aliases_owners, handle);

	if (!priv->queued_aliases)
		priv->queued_aliases = g_ptr_array_new();

	g_ptr_array_add(priv->queued_aliases,
		tp_value_array_build (2,
			G_TYPE_UINT, handle,
			G_TYPE_STRING, alias,
			G_TYPE_INVALID));
}

static GQuark
_canon_nick_quark (void) {
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
	IdleConnectionPrivate *priv = conn->priv;

	if (!priv->queued_aliases)
		return;

	tp_svc_connection_interface_aliasing_emit_aliases_changed(conn, priv->queued_aliases);

	g_ptr_array_foreach(priv->queued_aliases, _free_alias_pair, NULL);
	g_ptr_array_free(priv->queued_aliases, TRUE);
	priv->queued_aliases = NULL;

	tp_handle_set_destroy(priv->queued_aliases_owners);
	priv->queued_aliases_owners = NULL;
}

static void idle_connection_get_alias_flags(TpSvcConnectionInterfaceAliasing *iface, DBusGMethodInvocation *context) {
	tp_svc_connection_interface_aliasing_return_from_get_alias_flags(context, 0);
}

static const gchar *
gimme_an_alias (
    TpHandleRepoIface *repo,
    TpHandle handle)
{
  const gchar *alias = tp_handle_get_qdata (repo, handle, _canon_nick_quark());

  if (alias != NULL)
    return alias;
  else
    return tp_handle_inspect (repo, handle);
}

static void
conn_aliasing_fill_contact_attributes (
    GObject *obj,
    const GArray *contacts,
    GHashTable *attributes_hash)
{
  IdleConnection *self = IDLE_CONNECTION (obj);
  TpHandleRepoIface *repo = tp_base_connection_get_handles (
      TP_BASE_CONNECTION (self), TP_HANDLE_TYPE_CONTACT);
  guint i;

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      const gchar *alias = gimme_an_alias (repo, handle);

      g_assert (alias != NULL);

      tp_contacts_mixin_set_contact_attribute (attributes_hash,
          handle, TP_IFACE_CONNECTION_INTERFACE_ALIASING"/alias",
          tp_g_value_slice_new_string (alias));
    }
}

static void
idle_connection_get_aliases (
    TpSvcConnectionInterfaceAliasing *iface,
    const GArray *handles,
    DBusGMethodInvocation *context)
{
  TpHandleRepoIface *repo = tp_base_connection_get_handles (
      TP_BASE_CONNECTION (iface), TP_HANDLE_TYPE_CONTACT);
  GError *error = NULL;
  GHashTable *aliases;

  if (!tp_handles_are_valid (repo, handles, FALSE, &error))
    {
      dbus_g_method_return_error(context, error);
      g_error_free(error);
      return;
    }

  aliases = g_hash_table_new (NULL, NULL);

  for (guint i = 0; i < handles->len; i++)
    {
      TpHandle handle = g_array_index (handles, TpHandle, i);

      g_hash_table_insert (aliases, GUINT_TO_POINTER (handle),
          (gpointer) gimme_an_alias (repo, handle));
    }

  tp_svc_connection_interface_aliasing_return_from_get_aliases (context,
      aliases);
  g_hash_table_unref (aliases);
}

static void idle_connection_request_aliases(TpSvcConnectionInterfaceAliasing *iface, const GArray *handles, DBusGMethodInvocation *context) {
	TpHandleRepoIface *repo = tp_base_connection_get_handles(TP_BASE_CONNECTION(iface), TP_HANDLE_TYPE_CONTACT);
	GError *error = NULL;
	const gchar **aliases;

	if (!tp_handles_are_valid(repo, handles, FALSE, &error)) {
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return;
	}

	aliases = g_new0(const gchar *, handles->len + 1);
	for (guint i = 0; i < handles->len; i++) {
		TpHandle handle = g_array_index(handles, TpHandle, i);

		aliases[i] = gimme_an_alias (repo, handle);
	}

	tp_svc_connection_interface_aliasing_return_from_request_aliases(context, aliases);
	g_free(aliases);
}

static gboolean _send_rename_request(IdleConnection *obj, const gchar *nick, DBusGMethodInvocation *context) {
	TpHandleRepoIface *handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(obj), TP_HANDLE_TYPE_CONTACT);
	TpHandle handle = tp_handle_ensure(handles, nick, NULL, NULL);
	gchar msg[IRC_MSG_MAXLEN + 1];

	if (handle == 0) {
		GError error = {TP_ERROR, TP_ERROR_NOT_AVAILABLE, "Invalid nickname requested"};

		IDLE_DEBUG("failed to get handle for \"%s\"", nick);
		dbus_g_method_return_error(context, &error);

		return FALSE;
	}

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
		GError error = {TP_ERROR, TP_ERROR_NOT_AVAILABLE, "You can only set your own alias in IRC"};
		dbus_g_method_return_error(context, &error);

		return;
	}

	if (_send_rename_request(conn, requested_alias, context))
		tp_svc_connection_interface_aliasing_return_from_set_aliases(context);
}

static gboolean idle_connection_hton(IdleConnection *obj, const gchar *input, gchar **output, GError **_error) {
	IdleConnectionPrivate *priv = obj->priv;
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
		g_set_error(_error, TP_ERROR, TP_ERROR_NOT_AVAILABLE, "character set conversion failed: %s", error->message);
		g_error_free(error);
		*output = NULL;
		return FALSE;
	}

	*output = ret;
	return TRUE;
}

#define U_FFFD_REPLACEMENT_CHARACTER_UTF8 "\357\277\275"

static gchar *
idle_salvage_utf8 (gchar *supposed_utf8, gssize bytes)
{
	GString *salvaged = g_string_sized_new (bytes);
	const gchar *end;
	gchar *ret;
	gsize ret_len;

	while (!g_utf8_validate (supposed_utf8, bytes, &end)) {
		gssize valid_bytes = end - supposed_utf8;

		g_string_append_len (salvaged, supposed_utf8, valid_bytes);
		g_string_append_len (salvaged, U_FFFD_REPLACEMENT_CHARACTER_UTF8, 3);

		supposed_utf8 += (valid_bytes + 1);
		bytes -= (valid_bytes + 1);
	}

	g_string_append_len (salvaged, supposed_utf8, bytes);

	ret_len = salvaged->len;
	ret = g_string_free (salvaged, FALSE);

	/* It had better be valid now… */
	g_return_val_if_fail (g_utf8_validate (ret, ret_len, NULL), ret);
	return ret;
}


static gchar *
idle_connection_ntoh(IdleConnection *obj, const gchar *input) {
	IdleConnectionPrivate *priv = obj->priv;
	GError *error = NULL;
	gsize bytes_written;
	gchar *ret;
	gchar *p;

	if (input == NULL) {
		return NULL;
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
	} else if (!g_utf8_validate (ret, bytes_written, NULL)) {
		/* Annoyingly g_convert(UTF-8, UTF-8) doesn't filter out well-formed
		 * non-characters, so we have to do some further processing.
		 */
		gchar *salvaged;

		IDLE_DEBUG("Invalid UTF-8, salvaging what we can...");
		salvaged = idle_salvage_utf8(ret, bytes_written);
		g_free(ret);
		ret = salvaged;
	}

	return ret;
}

static void _aliasing_iface_init(gpointer g_iface, gpointer iface_data) {
	TpSvcConnectionInterfaceAliasingClass *klass = (TpSvcConnectionInterfaceAliasingClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_aliasing_implement_##x (\
		klass, idle_connection_##x)
	IMPLEMENT(get_alias_flags);
	IMPLEMENT(get_aliases);
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

