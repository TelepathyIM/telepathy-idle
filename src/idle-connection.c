/*
 * This file is part of telepathy-idle
 * 
 * Copyright (C) 2006 Nokia Corporation. All rights reserved.
 *
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
#include <telepathy-glib/handle-repo-dynamic.h>

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
#include "idle-im-channel.h"
#include "idle-muc-channel.h"

#include "idle-server-connection.h"
#include "idle-ssl-server-connection.h"
#include "idle-server-connection-iface.h"
#include "idle-server-connection-util.h"

#include "idle-connection.h"
#include "idle-connection-glue.h"
#include "idle-connection-signals-marshal.h"

#include "idle-version.h"

#define BUS_NAME 	"org.freedesktop.Telepathy.Connection.idle"
#define OBJECT_PATH "/org/freedesktop/Telepathy/Connection/idle"

G_DEFINE_TYPE(IdleConnection, idle_connection, G_TYPE_OBJECT);

#define IRC_PRESENCE_SHOW_AVAILABLE "available"
#define IRC_PRESENCE_SHOW_AWAY		"away"
#define IRC_PRESENCE_SHOW_OFFLINE	"offline"

#define ERROR_IF_NOT_CONNECTED(CONN, PRIV, ERROR) \
  if ((PRIV)->status != TP_CONNECTION_STATUS_CONNECTED) \
    { \
      g_debug ("%s: rejected request as disconnected", G_STRFUNC); \
      (ERROR) = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, \
                            "Connection is disconnected"); \
      return FALSE; \
    }

#define ERROR_IF_NOT_CONNECTED_ASYNC(CONN, PRIV, ERROR, CONTEXT) \
  if ((PRIV)->status != TP_CONNECTION_STATUS_CONNECTED) \
    { \
      g_debug ("%s: rejected request as disconnected", G_STRFUNC); \
      (ERROR) = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, \
                            "Connection is disconnected"); \
      dbus_g_method_return_error ((CONTEXT), (ERROR)); \
      g_error_free ((ERROR)); \
      return FALSE; \
    }

struct _IdleContactPresence
{
	IdlePresenceState presence_state;
	gchar *status_message;
	guint last_activity;
};

#define idle_contact_presence_new() (g_slice_new(IdleContactPresence))
#define idle_contact_presence_new0() (g_slice_new0(IdleContactPresence))

typedef struct _IdleStatusInfo IdleStatusInfo;

struct _IdleStatusInfo
{
	const gchar *name;
	TpConnectionPresenceType presence_type;
	const gboolean self, exclusive;
};

static const IdleStatusInfo idle_statuses[LAST_IDLE_PRESENCE_ENUM] =
{
	{IRC_PRESENCE_SHOW_AVAILABLE,	TP_CONNECTION_PRESENCE_TYPE_AVAILABLE, TRUE, TRUE},
	{IRC_PRESENCE_SHOW_AWAY,		TP_CONNECTION_PRESENCE_TYPE_AWAY, TRUE, TRUE},
	{IRC_PRESENCE_SHOW_OFFLINE,		TP_CONNECTION_PRESENCE_TYPE_OFFLINE, FALSE, TRUE}
};

void idle_contact_presence_free(IdleContactPresence *cp)
{
	g_free(cp->status_message);
	g_slice_free(IdleContactPresence, cp);
}

typedef struct
{
	DBusGMethodInvocation *ctx;
	gboolean suppress_handler;
} chan_req_data;

/* signal enum */
enum
{
    NEW_CHANNEL,
    PRESENCE_UPDATE,
    STATUS_CHANGED,
    DISCONNECTED,
	RENAMED,
    LAST_SIGNAL_ENUM
};

enum
{
	PROP_PROTOCOL = 1,
	PROP_NICKNAME,
	PROP_SERVER,
	PROP_PORT,
	PROP_PASSWORD,
	PROP_REALNAME,
	PROP_CHARSET,
	PROP_QUITMESSAGE,
	PROP_USE_SSL,
	LAST_PROPERTY_ENUM
};

static guint signals[LAST_SIGNAL_ENUM] = {0};

/* private structure */
typedef struct _IdleConnectionPrivate IdleConnectionPrivate;

struct _IdleConnectionPrivate
{
	/*
	 * network connection
	 */

	IdleServerConnectionIface *conn;
	guint sconn_status;
	guint sconn_timeout;

	/*
	 * disconnect reason
	 */
	
	TpConnectionStatusReason disconnect_reason;

	/* telepathy properties */
	char *protocol;

	/* IRC connection properties */
	char *nickname;
	char *server;
	guint port;
	char *password;
	char *realname;
	char *charset;
	char *quit_message;
	gboolean use_ssl;

	/* dbus object location */
	char *bus_name;
	char *object_path;

	/* info about us in CTCP VERSION format */
	char *ctcp_version_string;
	
	/* connection status */
	TpConnectionStatus status;
	
  /* self handle */
	TpHandle self_handle;

	/* channel request contexts */
	GHashTable *chan_req_ctxs;

	/* channels (for queries and regular IRC channels respectively) */
	GHashTable *im_channels;
	GHashTable *muc_channels;

	/* Set of handles whose presence status we should poll and associated polling timer */
	TpIntSet *polled_presences;
	guint presence_polling_timer_id;

	/* presence query queue, unload timer and pending reply list */
	GQueue *presence_queue;
	guint presence_unload_timer_id;
	GList *presence_reply_list;

	/* if Disconnected has been emitted */
	gboolean disconnected;

	/* continuation line buffer */
	gchar splitbuf[IRC_MSG_MAXLEN+3];
	
	/* output message queue */
	GQueue *msg_queue;

	/* UNIX time the last message was sent on */
	time_t last_msg_sent;

	/* GSource id for message queue unloading timeout */
	guint msg_queue_timeout;
	
	/* if idle_connection_dispose has already run once */
 	gboolean dispose_has_run;
};

#define IDLE_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), IDLE_TYPE_CONNECTION, IdleConnectionPrivate))

static void
idle_connection_init (IdleConnection *obj)
{
  IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE (obj);
  int i;
  
  priv->port = 6667;
  priv->password = NULL;
  priv->realname = NULL;
  priv->charset = NULL;
  priv->quit_message = NULL;
  priv->use_ssl = FALSE;

  priv->conn = NULL;
  priv->sconn_status = SERVER_CONNECTION_STATE_NOT_CONNECTED;
  
  priv->ctcp_version_string = g_strdup_printf("telepathy-idle %s Telepathy IM/VoIP framework http://telepathy.freedesktop.org", TELEPATHY_IDLE_VERSION);
  
  priv->chan_req_ctxs = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
  priv->im_channels = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_object_unref);
  priv->muc_channels = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_object_unref);

  for (i = 0; i < LAST_TP_HANDLE_TYPE; i++)
  {
    obj->handles[i] = NULL;
  }

  obj->handles[TP_HANDLE_TYPE_CONTACT] = (TpHandleRepoIface *)(g_object_new(TP_TYPE_DYNAMIC_HANDLE_REPO, "handle-type", TP_HANDLE_TYPE_CONTACT, NULL));
  obj->handles[TP_HANDLE_TYPE_ROOM] = (TpHandleRepoIface *)(g_object_new(TP_TYPE_DYNAMIC_HANDLE_REPO, "handle-type", TP_HANDLE_TYPE_ROOM, NULL));

  priv->polled_presences = tp_intset_new();
  priv->presence_polling_timer_id = 0;

  priv->presence_queue = g_queue_new();
  priv->presence_unload_timer_id = 0;
  priv->presence_reply_list = NULL;

  priv->status = TP_CONNECTION_STATUS_DISCONNECTED;

  memset(priv->splitbuf, 0, IRC_MSG_MAXLEN+3);
  priv->msg_queue = g_queue_new();
  priv->last_msg_sent = 0;
  priv->msg_queue_timeout = 0;

  priv->disconnected = FALSE;
}

static void idle_connection_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void idle_connection_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void idle_connection_dispose (GObject *object);
static void idle_connection_finalize (GObject *object);

static void idle_connection_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	IdleConnection *self = (IdleConnection *)(obj);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(self);
	
	switch (prop_id)
	{
		case PROP_PROTOCOL:
		{
			g_free(priv->protocol);
			priv->protocol = g_value_dup_string(value);
		}
		break;
		case PROP_NICKNAME:
		{
			g_free(priv->nickname);
			priv->nickname = g_value_dup_string(value);
		}
		break;
		case PROP_SERVER:
		{
			g_free(priv->server);
			priv->server = g_value_dup_string(value);
		}
		break;
		case PROP_PORT:
		{
			priv->port = g_value_get_uint(value);
		}
		break;
		case PROP_PASSWORD:
		{
			g_free(priv->password);
			priv->password = g_value_dup_string(value);
		}
		break;
		case PROP_REALNAME:
		{
			g_free(priv->realname);
			priv->realname = g_value_dup_string(value);
		}
		break;
		case PROP_CHARSET:
		{
			g_free(priv->charset);
			priv->charset = g_value_dup_string(value);
		}
		break;
		case PROP_QUITMESSAGE:
		{
			g_free(priv->quit_message);
			priv->quit_message = g_value_dup_string(value);
		}
		break;
		case PROP_USE_SSL:
		{
			g_debug("%s: setting use_ssl to %u", G_STRFUNC, priv->use_ssl);
			priv->use_ssl = g_value_get_boolean(value);
		}
		break;
		default:
		{
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		}
		break;
	}
}

static void idle_connection_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
	IdleConnection *self = (IdleConnection *)(obj);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(self);

	switch (prop_id)
	{
		case PROP_PROTOCOL:
		{
			g_value_set_string(value, priv->protocol);
		}
		break;
		case PROP_NICKNAME:
		{
			g_value_set_string(value, priv->nickname);
		}
		break;
		case PROP_SERVER:
		{
			g_value_set_string(value, priv->server);
		}
		break;
		case PROP_PORT:
		{
			g_value_set_uint(value, priv->port);
		}
		break;
		case PROP_PASSWORD:
		{
			g_value_set_string(value, priv->password);
		}
		break;
		case PROP_REALNAME:
		{
			g_value_set_string(value, priv->realname);
		}
		break;
		case PROP_CHARSET:
		{
			g_value_set_string(value, priv->charset);
		}
		break;
		case PROP_QUITMESSAGE:
		{
			g_value_set_string(value, priv->quit_message);
		}
		break;
		case PROP_USE_SSL:
		{
			g_value_set_boolean(value, priv->use_ssl);
		}
		break;
		default:
		{
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		}
		break;
	}
}

static void
idle_connection_class_init (IdleConnectionClass *idle_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (idle_connection_class);
  GParamSpec *param_spec;

  object_class->set_property = idle_connection_set_property;
  object_class->get_property = idle_connection_get_property;
  
  g_type_class_add_private (idle_connection_class, sizeof (IdleConnectionPrivate));

  object_class->dispose = idle_connection_dispose;
  object_class->finalize = idle_connection_finalize;

  /* params */

  param_spec = g_param_spec_string ("protocol", "Telepathy identifier for protocol",
                                    "Identifier string used when the protocol "
                                    "name is required. Unused internally.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PROTOCOL, param_spec);

  param_spec = g_param_spec_string ("nickname", "IRC nickname",
                                    "The nickname to be visible to others in IRC.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_NICKNAME, param_spec);

  param_spec = g_param_spec_string ("server", "Hostname or IP of the IRC server to connect to",
                                    "The server used when establishing the connection.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SERVER, param_spec);

  param_spec = g_param_spec_uint ("port", "IRC server port",
                                  "The destination port used when establishing the connection.",
                                  0, G_MAXUINT16, 6667,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PORT, param_spec);

  param_spec = g_param_spec_string  ("password", "Server password",
		  							"Password to authenticate to the server with",
									NULL,
									G_PARAM_READWRITE |
									G_PARAM_STATIC_NAME |
									G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PASSWORD, param_spec);

  param_spec = g_param_spec_string ("realname", "Real name",
		  							"The real name of the user connecting to IRC",
									NULL,
									G_PARAM_READWRITE |
									G_PARAM_STATIC_NAME |
									G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_REALNAME, param_spec);

  param_spec = g_param_spec_string ("charset", "Character set",
		  							"The character set to use to communicate with the outside world",
									NULL,
									G_PARAM_READWRITE |
									G_PARAM_STATIC_NAME |
									G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CHARSET, param_spec);

  param_spec = g_param_spec_string ("quit-message", "Quit message",
		  							"The quit message to send to the server when leaving IRC",
									NULL,
									G_PARAM_READWRITE |
									G_PARAM_STATIC_NAME |
									G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_QUITMESSAGE, param_spec);

  param_spec = g_param_spec_boolean ("use-ssl", "Use SSL",
		  							 "If the connection should use a SSL tunneled socket connection",
									 FALSE,
									 G_PARAM_READWRITE |
									 G_PARAM_STATIC_NAME |
									 G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_USE_SSL, param_spec);
									

  /* signals */

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

  signals[STATUS_CHANGED] =
    g_signal_new ("status-changed",
                  G_OBJECT_CLASS_TYPE (idle_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_connection_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[DISCONNECTED] =
  	  g_signal_new("disconnected",
  	  		  G_OBJECT_CLASS_TYPE(idle_connection_class),
  	  		  G_SIGNAL_RUN_LAST|G_SIGNAL_DETAILED,
  	  		  0,
  	  		  NULL, NULL,
  	  		  g_cclosure_marshal_VOID__VOID,
  	  		  G_TYPE_NONE, 0);

  signals[RENAMED] =
	  g_signal_new("renamed",
			  G_OBJECT_CLASS_TYPE(idle_connection_class),
			  G_SIGNAL_RUN_LAST|G_SIGNAL_DETAILED,
			  0,
			  NULL, NULL,
			  idle_connection_marshal_VOID__INT_INT,
			  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (idle_connection_class), &dbus_glib_idle_connection_object_info);
}

static void close_all_channels(IdleConnection *conn);

void
idle_connection_dispose (GObject *object)
{
  IdleConnection *self = IDLE_CONNECTION (object);
  IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE (self);
  DBusGProxy *dbus_proxy;

  dbus_proxy = tp_get_bus_proxy();

  if (priv->dispose_has_run)
  {
  	  return;
  }
  else
  {
  	  priv->dispose_has_run = TRUE;
  }

  g_debug("%s called", G_STRFUNC);

  if (priv->im_channels)
  {
  	  g_assert(g_hash_table_size(priv->im_channels) == 0);
  	  g_hash_table_destroy(priv->im_channels);
  }
  
  if (priv->muc_channels)
  {
  	  g_assert(g_hash_table_size(priv->muc_channels) == 0);
	  g_hash_table_destroy(priv->muc_channels);
  }

  if (priv->presence_polling_timer_id)
  {
	  g_source_remove(priv->presence_polling_timer_id);
	  priv->presence_polling_timer_id = 0;
  }

  if (priv->presence_unload_timer_id)
  {
	  g_source_remove(priv->presence_unload_timer_id);
	  priv->presence_unload_timer_id = 0;
  }
  
  if (priv->msg_queue_timeout)
  {
	  g_source_remove(priv->msg_queue_timeout);
	  priv->msg_queue_timeout = 0;
  }

  if (priv->sconn_timeout)
  {
	  g_source_remove(priv->sconn_timeout);
	  priv->sconn_timeout = 0;
  }

  if (priv->conn != NULL)
  {
  	  g_warning("%s: connection was open when the object was deleted, it'll probably crash now...", G_STRFUNC);

	  g_object_unref(priv->conn);
	  priv->conn = NULL;
  }

  dbus_g_proxy_call_no_reply(dbus_proxy, "ReleaseName", G_TYPE_STRING, priv->bus_name, G_TYPE_INVALID);

  if (G_OBJECT_CLASS (idle_connection_parent_class)->dispose)
    G_OBJECT_CLASS (idle_connection_parent_class)->dispose (object);
}

static void free_helper(gpointer data, gpointer unused)
{
	return g_free(data);
}

void
idle_connection_finalize (GObject *object)
{
  IdleConnection *self = IDLE_CONNECTION (object);
  IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE (self);
  gchar *nick;
  IdleOutputPendingMsg *msg;

  g_free(priv->protocol);
  g_free(priv->nickname);
  g_free(priv->server);
  g_free(priv->password);
  g_free(priv->realname);
  g_free(priv->charset);
  g_free(priv->quit_message);
  
  g_free(priv->bus_name);
  g_free(priv->object_path);

  g_free(priv->ctcp_version_string);

  g_hash_table_destroy(priv->chan_req_ctxs);

  tp_intset_destroy(priv->polled_presences);
  
  while ((nick = g_queue_pop_head(priv->presence_queue)) != NULL)
  {
	  g_free(nick);
  }
  
  g_queue_free(priv->presence_queue);

  while ((msg = g_queue_pop_head(priv->msg_queue)) != NULL)
  {
	  idle_output_pending_msg_free(msg);
  }

  g_queue_free(priv->msg_queue);

  if (priv->presence_reply_list)
  {
	  g_list_foreach(priv->presence_reply_list, free_helper, NULL);
	  g_list_free(priv->presence_reply_list);
  }

  G_OBJECT_CLASS (idle_connection_parent_class)->finalize (object);
}

gboolean _idle_connection_register(IdleConnection *conn, gchar **bus_name, gchar **object_path, GError **error)
{
	DBusGConnection *bus;
	DBusGProxy *bus_proxy;
	IdleConnectionPrivate *priv;
	const char *allowed_chars = "_1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	gchar *safe_proto;
	gchar *unique_name;
	guint request_name_result;
	GError *request_error;

	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));
	
	bus = tp_get_bus();
	bus_proxy = tp_get_bus_proxy();
	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	safe_proto = g_strdup(priv->protocol);
	g_strcanon(safe_proto, allowed_chars, '_');

	unique_name = g_strdup_printf("%s_%s", priv->nickname, priv->server);
	g_strcanon(unique_name, allowed_chars, '_');

	priv->bus_name = g_strdup_printf(BUS_NAME ".%s.%s", safe_proto, unique_name);
	priv->object_path = g_strdup_printf(OBJECT_PATH "/%s/%s", safe_proto, unique_name);

	g_free(safe_proto);
	g_free(unique_name);

	g_assert(error != NULL);

	if (!dbus_g_proxy_call(bus_proxy, "RequestName", &request_error,
							G_TYPE_STRING, priv->bus_name,
							G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
							G_TYPE_INVALID,
							G_TYPE_UINT, &request_name_result,
							G_TYPE_INVALID))
	{
		*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, request_error->message);
		return FALSE;
	}

	if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
	{
		gchar *msg;

		switch (request_name_result)
		{
			case DBUS_REQUEST_NAME_REPLY_IN_QUEUE:
			{
				msg = "Request has been queued, though we request non-queuing.";
			}
			break;
			case DBUS_REQUEST_NAME_REPLY_EXISTS:
			{
				msg = "A connection manager already has this busname in use.";
			}
			break;
			case DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER:
			{
				msg = "The connection manager already has this connection active.";
			}
			break;
			default:
			{
				msg = "Unknown error return from RequestName";
			}
			break;
		}

		*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "Error acquiring bus name %s, %s", priv->bus_name, msg);
		return FALSE;
	}

	g_debug("%s: bus_name %s", G_STRFUNC, priv->bus_name);

	dbus_g_connection_register_g_object(bus, priv->object_path, G_OBJECT(conn));

	g_debug("%s: object path %s", G_STRFUNC, priv->object_path);

	g_assert(bus_name != NULL);
	g_assert(object_path != NULL);
	
	*bus_name = g_strdup(priv->bus_name);
	*object_path = g_strdup(priv->object_path);

	return TRUE;
}

static void irc_handshakes(IdleConnection *conn);
static IdleIMChannel *new_im_channel(IdleConnection *conn, TpHandle handle, gboolean suppress_handler);
static IdleMUCChannel *new_muc_channel(IdleConnection *conn, TpHandle handle, gboolean suppress_handler);
static void connection_connect_cb(IdleConnection *conn, gboolean success);
static void connection_disconnect(IdleConnection *conn, TpConnectionStatusReason reason);
static void connection_disconnect_cb(IdleConnection *conn, TpConnectionStatusReason reason);
static void update_presence(IdleConnection *self, TpHandle contact_handle, IdlePresenceState presence_id, const gchar *status_message);
static void update_presence_full(IdleConnection *self, TpHandle contact_handle, IdlePresenceState presence_id, const gchar *status_message, guint last_activity);
static void connection_status_change(IdleConnection *conn, TpConnectionStatus status, TpConnectionStatusReason reason);
static void connection_message_cb(IdleConnection *conn, const gchar *msg);
static void emit_presence_update(IdleConnection *self, const TpHandle *contact_handles);
static void send_irc_cmd(IdleConnection *conn, const gchar *msg);
static void handle_err_erroneusnickname(IdleConnection *conn);
static void handle_err_nicknameinuse(IdleConnection *conn);
static void priv_rename(IdleConnection *conn, guint old, guint new);

static void handle_err_nicknameinuse(IdleConnection *conn)
{
	connection_status_change(conn, TP_CONNECTION_STATUS_DISCONNECTED, TP_CONNECTION_STATUS_REASON_NAME_IN_USE);
}

static void handle_err_erroneusnickname(IdleConnection *conn)
{
	connection_status_change(conn, TP_CONNECTION_STATUS_DISCONNECTED, TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED);
}

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

static gboolean msg_queue_timeout_cb(gpointer user_data);

static gboolean sconn_timeout_cb(gpointer user_data)
{
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	
	connection_connect_cb(conn, FALSE);
	connection_disconnect_cb(conn, TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
	
	return FALSE;
}

static void sconn_status_changed_cb(IdleServerConnectionIface *sconn, IdleServerConnectionState state, IdleServerConnectionStateReason reason, IdleConnection *conn)
{
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	g_debug("%s: called with state %u", G_STRFUNC, state);

	TpConnectionStatusReason tp_reason;

	switch (reason)
	{
		case SERVER_CONNECTION_STATE_REASON_ERROR:
		{
			tp_reason = TP_CONNECTION_STATUS_REASON_NETWORK_ERROR;
		}
		break;
		case SERVER_CONNECTION_STATE_REASON_REQUESTED:
		{
			tp_reason = TP_CONNECTION_STATUS_REASON_REQUESTED;
		}
		break;
		default:
		{
			g_assert_not_reached();
		}
		break;
	}
	
	if (priv->sconn_timeout)
	{
		g_source_remove(priv->sconn_timeout);
		priv->sconn_timeout = 0;
	}

	switch (state)
	{
		case SERVER_CONNECTION_STATE_NOT_CONNECTED:
		{
			if (priv->status == TP_CONNECTION_STATUS_CONNECTING)
			{
				connection_connect_cb(conn, FALSE);
				connection_disconnect_cb(conn, tp_reason);
			}
			else
			{
				connection_disconnect_cb(conn, reason);
			}
		}
		break;
		case SERVER_CONNECTION_STATE_CONNECTING:
		{
			priv->sconn_timeout = g_timeout_add(CONNECTION_TIMEOUT, sconn_timeout_cb, conn);
		}
		break;
		case SERVER_CONNECTION_STATE_CONNECTED:
		{
			if ((priv->msg_queue_timeout == 0) && (g_queue_get_length(priv->msg_queue) > 0))
			{
				g_debug("%s: we had messages in queue, start unloading them now", G_STRFUNC);

				priv->msg_queue_timeout = g_timeout_add(MSG_QUEUE_TIMEOUT, msg_queue_timeout_cb, conn);
			}
		}
		break;
		default:
		{
			g_assert_not_reached();
		}
		break;
	}

	priv->sconn_status = state;
}

static void split_message_cb(const gchar *msg, gpointer user_data)
{
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	gchar *converted;
	GError *err;

	if (!idle_connection_ntoh(conn, msg, &converted, &err))
	{
		g_debug("%s: ntoh: %s", G_STRFUNC, err->message);
		g_error_free(err);
		converted = (gchar *)(msg);
	}

	connection_message_cb(conn, converted);

	if (converted != msg)
	{
		g_free(converted);
	}
}

static void sconn_received_cb(IdleServerConnectionIface *sconn, gchar *raw_msg, IdleConnection *conn)
{
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	msg_split(raw_msg, priv->splitbuf, IRC_MSG_MAXLEN+3, split_message_cb, conn);
}

static void priv_rename(IdleConnection *conn, guint old, guint new)
{
	IdleConnectionPrivate *priv;
	MUCChannelRenameData data = {old, new};
	gpointer chan;
	IdleContactPresence *cp;

	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	if (old == new)
	{
		g_debug("%s: tried to rename with old == new? (%u, %u)", G_STRFUNC, old, new);

		return;
	}

	cp = idle_handle_get_presence(conn->handles[TP_HANDLE_TYPE_CONTACT], old);

	idle_handle_set_presence(conn->handles[TP_HANDLE_TYPE_CONTACT], new, cp);
	idle_handle_set_presence(conn->handles[TP_HANDLE_TYPE_CONTACT], old, NULL);

	if (old == priv->self_handle)
	{
		g_debug("%s: renaming self_handle (%u, %u)", G_STRFUNC, old, new);

		tp_handle_unref(conn->handles[TP_HANDLE_TYPE_CONTACT], old);

		priv->self_handle = new;

		tp_handle_ref(conn->handles[TP_HANDLE_TYPE_CONTACT], new);
	}

	chan = g_hash_table_lookup(priv->im_channels, GINT_TO_POINTER(old));

	if (chan != NULL)
	{
		g_hash_table_steal(priv->im_channels, GINT_TO_POINTER(old));

		g_hash_table_insert(priv->im_channels, GINT_TO_POINTER(new), chan);

		_idle_im_channel_rename((IdleIMChannel *)(chan), new);
	}

	g_hash_table_foreach(priv->muc_channels, muc_channel_rename_foreach, &data);
	
	g_signal_emit(conn, signals[RENAMED], 0, old, new);
}

gboolean _idle_connection_connect(IdleConnection *conn, GError **error)
{
	IdleConnectionPrivate *priv;

	g_assert(conn != NULL);
	g_assert(error != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	g_assert(priv->nickname != NULL);
	g_assert(priv->server != NULL);
	g_assert(priv->port > 0 && priv->port <= G_MAXUINT16);

			
	if (priv->conn == NULL)
	{
		GError *conn_error = NULL;
		IdleServerConnectionIface *sconn;
		gboolean valid;
		GType connection_type = (priv->use_ssl) 
										? IDLE_TYPE_SSL_SERVER_CONNECTION 
										: IDLE_TYPE_SERVER_CONNECTION;
		
		if ((priv->self_handle = idle_handle_for_contact(conn->handles[TP_HANDLE_TYPE_CONTACT], priv->nickname)) == 0)
		{
			g_debug("%s: invalid nickname %s", G_STRFUNC, priv->nickname);

			*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "invalid nickname %s", priv->nickname);

			return FALSE;
		}

    if (!priv->realname || !priv->realname[0])
    {
      const gchar *g_realname = g_get_real_name();

      g_free(priv->realname);

      if (g_realname && g_realname[0] && strcmp(g_realname, "Unknown"))
      {
        priv->realname = g_strdup(g_realname);
      }
      else
      {
        priv->realname = g_strdup(priv->nickname);
      }
    }

		valid = tp_handle_ref(conn->handles[TP_HANDLE_TYPE_CONTACT], priv->self_handle);

		g_assert(valid == TRUE);
		
		sconn = IDLE_SERVER_CONNECTION_IFACE(g_object_new(connection_type, 
														  "host", priv->server, 
														  "port", priv->port, 
														  NULL));

		g_signal_connect(sconn, "status-changed", (GCallback)(sconn_status_changed_cb), conn);

		if (!idle_server_connection_iface_connect(sconn, &conn_error))
		{
			g_debug("%s: server connection failed to connect: %s", G_STRFUNC, conn_error->message);
			*error = g_error_new(TP_ERRORS, TP_ERROR_NETWORK_ERROR, "failed to open low-level network connection: %s", conn_error->message);

			g_error_free(conn_error);
			g_object_unref(sconn);
			
			return FALSE;
		}

		priv->conn = sconn;
		
		g_signal_connect(sconn, "received", (GCallback)(sconn_received_cb), conn);
		
		irc_handshakes(conn);
	}
	else
	{
		g_debug("%s: conn already open!", G_STRFUNC);

		*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "connection already open!");

		return FALSE;
	}

	return TRUE;
}

gboolean _idle_connection_send(IdleConnection *conn, const gchar *msg)
{
	send_irc_cmd(conn, msg);

	return TRUE;
}

static gboolean msg_queue_timeout_cb(gpointer user_data)
{
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	int i, j;
	IdleOutputPendingMsg *output_msg;
	gchar msg[IRC_MSG_MAXLEN+3];
	GError *error;

	g_debug("%s: called", G_STRFUNC);
	
	if (priv->sconn_status != SERVER_CONNECTION_STATE_CONNECTED)
	{
		g_debug("%s: connection was not connected!", G_STRFUNC);

		priv->msg_queue_timeout = 0;

		return FALSE;
	}

	output_msg = g_queue_peek_head(priv->msg_queue);

	if (output_msg == NULL)
	{
		priv->msg_queue_timeout = 0;
		
		return FALSE;
	}

	g_strlcpy(msg, output_msg->message, IRC_MSG_MAXLEN+3);
	
	for (i=1; i < MSG_QUEUE_UNLOAD_AT_A_TIME; i++)
	{
		output_msg = g_queue_peek_nth(priv->msg_queue, i);

		if ((output_msg != NULL) && ((strlen(msg) + strlen(output_msg->message)) < IRC_MSG_MAXLEN+2))
		{
			strcat(msg, output_msg->message);
		}
		else
		{
			break;
		}
	}

	if (idle_server_connection_iface_send(priv->conn, msg, &error))
	{
		for (j=0; j<i; j++)
		{
			output_msg = g_queue_pop_head(priv->msg_queue);

			idle_output_pending_msg_free(output_msg);
		}

		priv->last_msg_sent = time(NULL);
	}
	else
	{
		g_debug("%s: low-level network connection failed to send: %s", G_STRFUNC, error->message);

		g_error_free(error);		
	}

	return TRUE;
}

/**
 * Queue a IRC command for sending, clipping it to IRC_MSG_MAXLEN bytes and appending the required <CR><LF> to it
 */
static void send_irc_cmd_full(IdleConnection *conn, const gchar *msg, guint priority)
{
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

	if (!idle_connection_hton(conn, cmd, &converted, &convert_error))
	{
		g_debug("%s: hton: %s", G_STRFUNC, convert_error->message);
		g_error_free(convert_error);
		converted = g_strdup(cmd);
	}

	if ((priority == SERVER_CMD_MAX_PRIORITY)
          || ((priv->status == TP_CONNECTION_STATUS_CONNECTED)
            && (priv->msg_queue_timeout == 0)
			&& (curr_time - priv->last_msg_sent > MSG_QUEUE_TIMEOUT)))
	{
		priv->last_msg_sent = curr_time;
		
		if (!idle_server_connection_iface_send(priv->conn, converted, &error))
		{
			g_debug("%s: server connection failed to send: %s", G_STRFUNC, error->message);
			g_error_free(error);
		}
		else
		{
			g_free(converted);
			return;
		}
	}				
	
	output_msg = idle_output_pending_msg_new();
	output_msg->message = converted;
	output_msg->priority = priority;
	
	g_queue_insert_sorted(priv->msg_queue, output_msg, pending_msg_compare, NULL);

	if (priv->msg_queue_timeout == 0)
	{
		priv->msg_queue_timeout = g_timeout_add(MSG_QUEUE_TIMEOUT*1024, msg_queue_timeout_cb, conn);
	}
}

static void send_irc_cmd(IdleConnection *conn, const gchar *msg)
{
	return send_irc_cmd_full(conn, msg, SERVER_CMD_NORMAL_PRIORITY);	
}

static void cmd_parse(IdleConnection *conn, const gchar *msg);
static gchar *prefix_cmd_parse(IdleConnection *conn, const gchar *msg);
static gchar *prefix_numeric_parse(IdleConnection *conn, const gchar *msg);

static void connection_message_cb(IdleConnection *conn, const gchar *msg)
{
	gchar scanf_fool[IRC_MSG_MAXLEN+2];
	int scanf_numeric;
	int cmdcount, numericcount;
    gchar *reply = NULL;

	IdleConnectionPrivate *priv;
	
	if (msg == NULL || msg[0] == '\0')
	{
		return;
	}

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	if (msg[0] != ':')
	{
		cmd_parse(conn, msg);
	}
	else if ((numericcount = sscanf(msg, ":%s %i %s", scanf_fool, &scanf_numeric, scanf_fool)) == 3)
	{
		reply = prefix_numeric_parse(conn, msg+1);
	}
	else if ((cmdcount = sscanf(msg, ":%s %s %s", scanf_fool, scanf_fool, scanf_fool)) == 3)
	{
		reply = prefix_cmd_parse(conn, msg+1);
	}
	else
	{
		g_debug("%s: unrecognized message format from server (%i cmd, %i numeric) (%s)", G_STRFUNC, cmdcount, numericcount, msg);
		return;
	}

	if (reply)
	{
		send_irc_cmd (conn, reply);
		g_free (reply);
	}
}

static void cmd_parse(IdleConnection *conn, const gchar *msg)
{	
	if ((g_strncasecmp(msg, "PING ", 5) == 0) && (msg[5] != '\0'))
	{
		/* PING command, reply ... */
		gchar *reply = g_strdup_printf("PONG %s", msg+5);
		send_irc_cmd_full (conn, reply, SERVER_CMD_MAX_PRIORITY);
		g_free (reply);
	}
	else
	{
		g_debug("%s: ignored unparsed message from server (%s)", G_STRFUNC, msg);
	}
}

static void muc_channel_handle_quit_foreach(gpointer key, gpointer value, gpointer user_data)
{
	IdleMUCChannel *chan = value;
	TpHandle handle = GPOINTER_TO_INT(user_data);

	g_debug("%s: for %p %p %p", G_STRFUNC, key, value, user_data);

	_idle_muc_channel_handle_quit(chan, handle, TRUE, handle, TP_CHANNEL_GROUP_CHANGE_REASON_OFFLINE);
}

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
			
		if (g_unichar_isalpha(ucs4char))
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
		else if ((recipient[0] == '#') || (recipient[0] == '&') || (recipient[0] == '!') || (recipient[0] == '+'))
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
	else if (g_strncasecmp(cmd, "INVITE", 6) == 0)
	{
		TpHandle handle;
		IdleMUCChannel *chan;
		gboolean worked_around = FALSE;
		gchar *tmp;
		TpHandle inviter_handle;

		if (tokenc != 4)
		{
			g_debug("%s: got INVITE with tokenc != 4, ignoring...", G_STRFUNC);
			goto cleanupl;
		}

		/* workaround for non-conformant servers... */
		if (tokens[3][0] == ':')
		{
			tokens[3]++;
			worked_around = TRUE;
		}

		handle = idle_handle_for_room(conn->handles[TP_HANDLE_TYPE_ROOM], tokens[3]);

		if (worked_around)
		{
			tokens[3]--;
		}

		if (handle == 0)
		{
			g_debug("%s: failed to get handle for (%s) in INVITE", G_STRFUNC, tokens[3]);
			goto cleanupl;
		}

		tmp = strchr(tokens[0], '!');

		if (tmp)
		{
			*tmp = '\0';
		}

		inviter_handle = idle_handle_for_contact(conn->handles[TP_HANDLE_TYPE_CONTACT], tokens[0]);

		if (!inviter_handle)
		{
			g_debug("%s: failed to get handle for inviter (%s) in INVITE", G_STRFUNC, tokens[0]);
		}

		chan = g_hash_table_lookup(priv->muc_channels, GINT_TO_POINTER(handle));

		if (chan != NULL)
		{
			g_debug("%s: got INVITE for a channel we are already on, ignoring...", G_STRFUNC);
			goto cleanupl;
		}

		chan = new_muc_channel(conn, handle, FALSE);

		g_assert(chan != NULL);

		_idle_muc_channel_invited(chan, inviter_handle);

		g_debug("%s: got INVITEd to channel (%s) (handle %u)", G_STRFUNC, tokens[3], handle);
	}
	else if (g_strncasecmp(cmd, "QUIT", 4) == 0)
	{
		TpHandle handle;
		IdleIMChannel *chan;

		handle = idle_handle_for_contact(conn->handles[TP_HANDLE_TYPE_CONTACT], from);

		if (handle == 0)
		{
			g_debug("%s: could not get handle for (%s) in QUIT", G_STRFUNC, from);
			goto cleanupl;
		}
		
		chan = g_hash_table_lookup(priv->im_channels, GINT_TO_POINTER(handle));

		if (chan != NULL)
		{
			g_debug("%s: contact QUIT, closing IMChannel...", G_STRFUNC);
			
			g_hash_table_remove(priv->im_channels, GINT_TO_POINTER(handle));
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

static gint strcasecmp_helper(gconstpointer a, gconstpointer b)
{
	return strcasecmp(a, b);
}

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

	if (numeric == IRC_RPL_NAMEREPLY)
	{
		char *identifier;
		char *channeltmp;
		char *channel;
		TpHandle handle;
		GArray *names_array;
		guint i, lasti;
		guint length;
		IdleMUCChannel *chan;
		
		if ((identifier = strchr(msg, '=')) != NULL)
		{
			g_debug("%s: found identifier for PUBLIC channel at %p", G_STRFUNC, identifier);
		}
		else if ((identifier = strchr(msg, '*')) != NULL)
		{
			g_debug("%s: found identifier for PRIVATE channel at %p", G_STRFUNC, identifier);
		}
		else if ((identifier = strchr(msg, '@')) != NULL)
		{
			g_debug("%s: found identifier for SECRET channel at %p", G_STRFUNC, identifier);
		}
		else
		{
			g_debug("%s: did not find channel type identifier in NAMES msg, ignoring", G_STRFUNC);
			goto cleanupl;
		}

		channeltmp = identifier+1;

		identifier = strstr(identifier, " :")+1;

		if (!identifier)
		{
			g_debug("%s: did not find body identifier in NAMES msg, ignoring", G_STRFUNC);
			goto cleanupl;
		}

		*identifier = '\0';
		
		channeltmp = g_strdup(channeltmp);
		
		channel = g_strstrip(channeltmp);

		handle = idle_handle_for_room(conn->handles[TP_HANDLE_TYPE_ROOM], channel);

		if (handle == 0)
		{
			g_debug("%s: got invalid channel (%s) in NAMES, ignoring", G_STRFUNC, channel);

			g_free(channeltmp);
			
			goto cleanupl;
		}

		names_array = g_array_new(FALSE, FALSE, sizeof(gchar *));
		
		identifier++;

		length = strlen(identifier);
		lasti = 0;

		for (i=0; i < length; i++)
		{
			if (identifier[i] == ' ')
			{
				if (i>lasti)
				{
					gchar *tmp;

					identifier[i] = '\0';

					tmp = g_strdup(identifier+lasti);
					
					g_array_append_val(names_array, tmp);
				}
				
				lasti = i+1;
			}
		}

		chan = (IdleMUCChannel *)(g_hash_table_lookup(priv->muc_channels, GINT_TO_POINTER(handle)));

		if (chan == NULL)
		{
			g_debug("%s: got NAMES for channel (%s) we're not on but have a handle (%u) for?", G_STRFUNC, channel, handle);

			g_free(channeltmp);

			goto cleanupl;
		}

		_idle_muc_channel_names(chan, names_array);

		g_debug("%s: parsed %u names for channel %s (handle %u)", G_STRFUNC, names_array->len, channel, handle);
		
		g_free(channeltmp);
		
		for (i=0; i<names_array->len; i++)
		{
			g_free(g_array_index(names_array, gchar *, i));
		}
		g_array_free(names_array, TRUE);
	}
	else if (numeric == IRC_ERR_BADCHANNELKEY)
	{
		gchar *channel;
		gchar *tmp;
		TpHandle handle;
		IdleMUCChannel *chan;

	 	if (((channel = strchr(msg, '#')) == NULL) && 
			((channel = strchr(msg, '!')) == NULL) &&
			((channel = strchr(msg, '&')) == NULL) &&
			((channel = strchr(msg, '+')) == NULL))
		{
			g_debug("%s: didn't find valid channel name in ERR_BADCHANNELKEY (%s), ignoring", G_STRFUNC, msg);
			goto cleanupl;
		}

		if ((tmp = strstr(channel, " :")+1) == NULL)
		{
			g_debug("%s: failed to find : separator in ERR_BADCHANNELKEY (%s), ignoring", G_STRFUNC, msg);
			goto cleanupl;
		}

		*tmp = '\0';

		channel = g_strstrip(channel);

		handle = idle_handle_for_room(conn->handles[TP_HANDLE_TYPE_ROOM], channel);

		if (handle == 0)
		{
			g_debug("%s: failed to get handle for channel %s in ERR_BADCHANNELKEY (%s)", G_STRFUNC, channel, msg);
			goto cleanupl;
		}

		chan = (IdleMUCChannel *)(g_hash_table_lookup(priv->muc_channels, GINT_TO_POINTER(handle)));

		if (chan == NULL)
		{
			g_debug("%s: got ERR_BADCHANNELKEY for channel %s we are not on yet have a handle (%u) for?", G_STRFUNC, channel, handle);
			goto cleanupl;
		}

		_idle_muc_channel_badchannelkey(chan);

		g_debug("%s: got ERR_BADCHANNELKEY for channel %s (handle %u)", G_STRFUNC, channel, handle);
	}
	else if (numeric == IRC_ERR_ERRONEOUSNICKNAME)
	{
		g_debug("%s: got ERR_ERROUNEUSNICKNAME", G_STRFUNC);

		handle_err_erroneusnickname(conn);
	}
	else if (numeric == IRC_ERR_NICKNAMEINUSE)
	{
		g_debug("%s: got ERR_NICKNAMEINUSE", G_STRFUNC);

		handle_err_nicknameinuse(conn);
	}
	else if (numeric == IRC_RPL_WELCOME)
	{
		char *nick = recipient;
		g_debug("%s: got RPL_WELCOME with nick %s", G_STRFUNC, nick);

		if (strcmp(priv->nickname, nick))
		{
			g_debug("%s: nick different from original (%s -> %s), renaming", G_STRFUNC, priv->nickname, nick);
			g_free(priv->nickname);
			priv->nickname = g_strdup(nick);

			if (priv->self_handle)
			{
				idle_handle_unref(priv->handles, TP_HANDLE_TYPE_CONTACT, priv->self_handle);
			}

			priv->self_handle = idle_handle_for_contact(priv->handles, nick);
			idle_handle_ref(priv->handles, TP_HANDLE_TYPE_CONTACT, priv->self_handle);
		}

		connection_connect_cb(conn, TRUE);

		update_presence(conn, priv->self_handle, IDLE_PRESENCE_AVAILABLE, NULL);
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
	else if (numeric == IRC_ERR_NOSUCHNICK)
	{
		TpHandle handle;
		GList *link;

		if (tokenc < 4)
		{
			g_debug("%s: got ERR_NOSUCHNICK with tokenc < 4, ignoring...", G_STRFUNC);
			goto cleanupl;
		}

		if ((link = g_list_find_custom(priv->presence_reply_list, tokens[3], strcasecmp_helper)) != NULL)
		{
			g_debug("%s: one of the contacts we asked presence for was offline, removing query...", G_STRFUNC);
			g_free(link->data);
			priv->presence_reply_list = g_list_delete_link(priv->presence_reply_list, link);
		}
		
		handle = idle_handle_for_contact(conn->handles[TP_HANDLE_TYPE_CONTACT], tokens[3]);

		if (handle == 0)
		{
			g_debug("%s: could not get handle for (%s) in ERR_NOSUCHNICK", G_STRFUNC, tokens[3]);
			goto cleanupl;
		}

		update_presence(conn, handle, IDLE_PRESENCE_OFFLINE, NULL);

		g_debug("%s: got ERR_NOSUCHNICK for (%s) (handle %u)", G_STRFUNC, tokens[3], handle);
	}
	else if (numeric == IRC_ERR_BANNEDFROMCHAN 
			|| numeric == IRC_ERR_CHANNELISFULL
			|| numeric == IRC_ERR_INVITEONLYCHAN)
	{
		TpHandle handle;
		IdleMUCChannel *chan;
		guint err;

		handle = idle_handle_for_room(conn->handles[TP_HANDLE_TYPE_ROOM], tokens[3]);

		if (handle == 0)
		{
			g_debug("%s: failed to get handle for (%s) in join errors", G_STRFUNC, tokens[2]);
			goto cleanupl;
		}

		chan = g_hash_table_lookup(priv->muc_channels, GINT_TO_POINTER(handle));

		if (chan == NULL)
		{
			g_debug("%s: failed to find channel with handle %u in join errors", G_STRFUNC, handle);
			goto cleanupl;
		}

		switch (numeric)
		{
			case IRC_ERR_BANNEDFROMCHAN:
			{
				err = MUC_CHANNEL_JOIN_ERROR_BANNED;
			}
			break;
			case IRC_ERR_CHANNELISFULL:
			{
				err = MUC_CHANNEL_JOIN_ERROR_FULL;
			}
			break;
			case IRC_ERR_INVITEONLYCHAN:
			{
				err = MUC_CHANNEL_JOIN_ERROR_INVITE_ONLY;
			}
			break;
			default:
			{
				g_assert_not_reached();
			}
			break;
		}

		_idle_muc_channel_join_error(chan, err);
	}
	else if (numeric == IRC_RPL_NOWAWAY
			|| numeric == IRC_RPL_UNAWAY)
	{
		update_presence(conn, priv->self_handle, 
				(numeric == IRC_RPL_UNAWAY) ? IDLE_PRESENCE_AVAILABLE : IDLE_PRESENCE_AWAY, NULL);

		g_debug("%s: got away state change", G_STRFUNC);
	}
	else if (numeric == IRC_RPL_AWAY)
	{
		TpHandle handle;
		char *tmp;
		GList *link;
		
		if (tokenc < 5)
		{
			g_debug("%s: got RPL_AWAY with tokenc < 3, ignoring...", G_STRFUNC);
			goto cleanupl;
		}

		handle = idle_handle_for_contact(conn->handles[TP_HANDLE_TYPE_CONTACT], tokens[3]);

		if (handle == 0)
		{
			g_debug("%s: could not get handle for (%s) in IRC_RPL_AWAY...", G_STRFUNC, tokens[3]);
			goto cleanupl;
		}

		tmp = strstr(msg, " :")+1;

		if (tmp == NULL)
		{
			g_debug("%s: failed to find body separator in RPL_AWAY, ignoring...", G_STRFUNC);
			goto cleanupl;
		}

		g_debug("%s: got RPL_AWAY for %u", G_STRFUNC, handle);

		if ((link = g_list_find_custom(priv->presence_reply_list, tokens[3], strcasecmp_helper)) != NULL)
		{
			g_debug("%s: got RPL_AWAY for someone we had queried presence for...", G_STRFUNC);
			g_free(link->data);
			priv->presence_reply_list = g_list_remove_link(priv->presence_reply_list, link);
		}

		update_presence(conn, handle, IDLE_PRESENCE_AWAY, tmp+1);
	}
	else if (numeric == IRC_RPL_ENDOFWHOIS)
	{
		GList *link;
		TpHandle handle;
		
		if (tokenc < 4)
		{
			g_debug("%s: got IRC_RPL_ENDOFWHOIS with <4 tokens, ignoring...", G_STRFUNC);
			goto cleanupl;
		}

		if ((link = g_list_find_custom(priv->presence_reply_list, tokens[3], strcasecmp_helper)) != NULL)
		{
			g_debug("%s: got end of whois for someone we have queried presence for but haven't got away/offline yet -> available", G_STRFUNC);

			g_free(link->data);
			priv->presence_reply_list = g_list_remove_link(priv->presence_reply_list, link);

			handle = idle_handle_for_contact(conn->handles[TP_HANDLE_TYPE_CONTACT], tokens[3]);

			if (handle == 0)
			{
				g_debug("%s: could not get handle for (%s) in IRC_RPL_ENDOFWHOIS...", G_STRFUNC, tokens[3]);
				goto cleanupl;
			}

			update_presence(conn, handle, IDLE_PRESENCE_AVAILABLE, NULL);
		}
	}
	else if (numeric == IRC_RPL_WHOISIDLE)
	{
		TpHandle handle;
		IdleContactPresence *cp;
		guint last_activity;

		if (tokenc < 5)
		{
			g_debug("%s: got IRC_RPL_WHOISIDLE with tokenc < 5, ignoring...", G_STRFUNC);
			goto cleanupl;
		}

		handle = idle_handle_for_contact(conn->handles[TP_HANDLE_TYPE_CONTACT], tokens[2]);

		if (handle == 0)
		{
			g_debug("%s: failed to get handle for (%s) in RPL_WHOISIDLE, ignoring...", G_STRFUNC, tokens[2]);
			goto cleanupl;
		}

		cp = idle_handle_get_presence(conn->handles[TP_HANDLE_TYPE_CONTACT], handle);

		if (cp == NULL)
		{
			g_debug("%s: failed to get cp in RPL_WHOISIDLE, ignoring...", G_STRFUNC);
			goto cleanupl;
		}

		last_activity = time(NULL) - atoi(tokens[3]);

		update_presence_full(conn, handle, cp->presence_state, NULL, last_activity);

		g_debug("%s: got RPL_WHOISIDLE for (%s) (handle %u)", G_STRFUNC, tokens[2], handle);
	}
	else
	{
		g_debug("%s: ignored unparsed message from server (%s)", G_STRFUNC, msg);
	}

cleanupl:

	g_strfreev(tokens);
	
	return reply;
}

static void irc_handshakes(IdleConnection *conn)
{
	IdleConnectionPrivate *priv;
	gchar msg[IRC_MSG_MAXLEN+1];
	
	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	if ((priv->password != NULL) && (priv->password[0] != '\0'))
	{
		g_snprintf(msg, IRC_MSG_MAXLEN+1, "PASS %s", priv->password);

		send_irc_cmd_full(conn, msg, SERVER_CMD_NORMAL_PRIORITY + 1);
	}

	g_snprintf(msg, IRC_MSG_MAXLEN+1, "NICK %s", priv->nickname);
	send_irc_cmd(conn, msg);

	g_snprintf(msg, IRC_MSG_MAXLEN+1, "USER %s %u * :%s", priv->nickname, 8, priv->realname);
	send_irc_cmd(conn, msg);
}

static void connection_connect_cb(IdleConnection *conn, gboolean success)
{
	g_assert(conn != NULL);
	
	if (success)
	{
		connection_status_change(conn, TP_CONNECTION_STATUS_CONNECTED, TP_CONNECTION_STATUS_REASON_REQUESTED);
	}
	else
	{
		connection_status_change(conn, TP_CONNECTION_STATUS_DISCONNECTED, TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
	}
}

/* 2 minutes */
#define PRESENCE_POLLING_TIMEOUT 2*60*1000

static void presence_request_push(IdleConnection *obj, const gchar *nick);

static void polling_foreach_func(guint i, gpointer user_data)
{
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	TpHandle handle = (TpHandle)(i);

	if (tp_handle_is_valid(conn->handles[TP_HANDLE_TYPE_CONTACT], handle, NULL))
	{
		const gchar *nick = idle_handle_inspect(conn->handles[TP_HANDLE_TYPE_CONTACT], handle);

		presence_request_push(conn, nick);
	}
	else
	{
		tp_intset_remove(priv->polled_presences, handle);
	}
}

static gboolean presence_polling_cb(gpointer user_data)
{
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(user_data);

	tp_intset_foreach(priv->polled_presences, polling_foreach_func, conn);

	if (!tp_intset_size(priv->polled_presences))
	{
		return FALSE;
	}

	return TRUE;
}

static void polled_presence_add(IdleConnection *conn, TpHandle handle)
{
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	
	tp_intset_add(priv->polled_presences, handle);

	if (!priv->presence_polling_timer_id)
	{
		presence_polling_cb(conn);
		priv->presence_polling_timer_id = g_timeout_add(PRESENCE_POLLING_TIMEOUT, presence_polling_cb, conn);
	}
}

static void polled_presence_remove(IdleConnection *conn, TpHandle handle)
{
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	
	tp_intset_remove(priv->polled_presences, handle);

	if (priv->presence_polling_timer_id && !tp_intset_size(priv->polled_presences))
	{
		g_source_remove(priv->presence_polling_timer_id);
		priv->presence_polling_timer_id = 0;
	}
}

static void update_presence(IdleConnection *self, TpHandle contact_handle, IdlePresenceState presence_state, const gchar *status_message)
{
	return update_presence_full(self, contact_handle, presence_state, status_message, 0);
}

static void update_presence_full(IdleConnection *self, TpHandle contact_handle, IdlePresenceState presence_state, const gchar *status_message, guint last_activity)
{
	IdleContactPresence *cp = idle_handle_get_presence(self->handles[TP_HANDLE_TYPE_CONTACT], contact_handle);
	TpHandle handles[2] = {contact_handle, 0};

	if (cp)
	{
		if (cp->presence_state == presence_state &&
			((cp->status_message == NULL && status_message == NULL) ||
			 (cp->status_message != NULL && status_message != NULL &&
			  strcmp(cp->status_message, status_message) == 0)))
		{
			return;
		}

		if (presence_state == IDLE_PRESENCE_AVAILABLE)
		{
			polled_presence_remove(self, contact_handle);
			idle_contact_presence_free(cp);
			idle_handle_set_presence(self->handles[TP_HANDLE_TYPE_CONTACT], contact_handle, NULL);
		}
	}
	else if (presence_state != IDLE_PRESENCE_AVAILABLE)
	{
		cp = idle_contact_presence_new0();
		g_assert(idle_handle_set_presence(self->handles[TP_HANDLE_TYPE_CONTACT], contact_handle, cp));

		polled_presence_add(self, contact_handle);
	}
	else
	{
		goto emit;
	}

	cp->presence_state = presence_state;

	g_free(cp->status_message);

	if (status_message && status_message[0] != '\0')
	{
		cp->status_message = g_strdup(status_message);
	}
	else
	{
		cp->status_message = NULL;
	}

	if (last_activity != 0)
	{
		cp->last_activity = last_activity;
	}

emit:
	emit_presence_update(self, handles);
}

static void connection_status_change(IdleConnection *conn, TpConnectionStatus status, TpConnectionStatusReason reason)
{
	IdleConnectionPrivate *priv;

	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	g_debug("%s: status %u reason %u", G_STRFUNC, status, reason);

	if (priv->status != status)
	{
		priv->status = status;

		g_debug("%s: emitting status-changed with status %u reason %u", G_STRFUNC, status, reason);
		g_signal_emit(conn, signals[STATUS_CHANGED], 0, status, reason);
	}
}

static void im_channel_closed_cb(IdleIMChannel *chan, gpointer user_data);
static void muc_channel_closed_cb(IdleMUCChannel *chan, gpointer user_data);

static void close_all_channels(IdleConnection *conn)
{
	IdleConnectionPrivate *priv;

	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	if (priv->im_channels)
	{
		GHashTable *tmp = priv->im_channels;
		priv->im_channels = NULL;
		g_hash_table_destroy(tmp);
	}
	
	if (priv->muc_channels)
	{
		GHashTable *tmp = priv->muc_channels;
		priv->muc_channels = NULL;
		g_hash_table_destroy(tmp);
	}
}

static void send_quit_request(IdleConnection *conn)
{
	IdleConnectionPrivate *priv;
	gchar cmd[IRC_MSG_MAXLEN+1];

	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	g_snprintf(cmd, IRC_MSG_MAXLEN+1, "QUIT :%s", priv->quit_message);

	send_irc_cmd(conn, cmd);
}

static void connection_disconnect(IdleConnection *conn, TpConnectionStatusReason reason)
{
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

static void connection_disconnect_cb(IdleConnection *conn, TpConnectionStatusReason reason)
{
	IdleConnectionPrivate *priv;

	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	
	g_debug("%s: called with reason %u", G_STRFUNC, reason);

	close_all_channels(conn);
	connection_status_change(conn, TP_CONNECTION_STATUS_DISCONNECTED, reason);

	if (priv->conn)
	{
		g_signal_handlers_disconnect_by_func(priv->conn, (GCallback)(sconn_status_changed_cb), conn);
		g_object_unref(priv->conn);
		priv->conn = NULL;
	}

	if (priv->msg_queue_timeout)
	{
		g_source_remove(priv->msg_queue_timeout);
		priv->msg_queue_timeout = 0;
	}
	
	if (!priv->disconnected)
	{
		priv->disconnected = TRUE;
		g_debug("%s: emitting DISCONNECTED", G_STRFUNC);
		g_signal_emit(conn, signals[DISCONNECTED], 0);
	}
}

struct member_check_data
{
	TpHandle handle;
	gboolean is_present;
};

static void member_check_foreach(gpointer key, gpointer value, gpointer user_data)
{
	IdleMUCChannel *chan = IDLE_MUC_CHANNEL(value);
	struct member_check_data *data = (struct member_check_data *)(user_data);
	
	if (!data->is_present && _idle_muc_channel_has_current_member(chan, data->handle))
	{
		data->is_present = TRUE;
	}
}

const IdleContactPresence *get_contact_presence(IdleConnection *conn, TpHandle handle)
{
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	const IdleContactPresence *ret;

	ret = idle_handle_get_presence(conn->handles[TP_HANDLE_TYPE_CONTACT], handle);

	if (!ret)
	{
		const static IdleContactPresence available_presence = {IDLE_PRESENCE_AVAILABLE, NULL, 0};
		const static IdleContactPresence offline_presence = {IDLE_PRESENCE_OFFLINE, NULL, 0};
		
		if (g_hash_table_lookup(priv->im_channels, GUINT_TO_POINTER(handle)) != NULL)
		{
			ret = &available_presence;
		}
		else
		{
			struct member_check_data data = {handle, FALSE};
			
			g_hash_table_foreach(priv->muc_channels, member_check_foreach, &data);

			if (data.is_present)
			{
				ret = &available_presence;
			}
			else
			{
				ret = &offline_presence;
			}
		}
	}

	return ret;
}

static void
bastard_destroyer_from_collabora(GValue *value)
{
	g_value_unset(value);
	g_free(value);
}

static void emit_presence_update(IdleConnection *self, const TpHandle *contact_handles)
{
	IdleConnectionPrivate *priv;
	const IdleContactPresence *cp; 
	GHashTable *presence;
	GValueArray *vals;
	GHashTable *contact_status, *parameters;
	int i;

	g_assert(self != NULL);
	g_assert(IDLE_IS_CONNECTION(self));

	priv = IDLE_CONNECTION_GET_PRIVATE(self);

	presence = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)(g_value_array_free));
	
	for (i = 0; contact_handles[i] != 0; i++)
	{
		GValue *message;

		cp = get_contact_presence(self, contact_handles[i]);

		if (cp == NULL)
		{
			g_debug("%s: did not find presence for %u!!", G_STRFUNC, contact_handles[i]);
			continue;
		}

		message = g_new0(GValue, 1);
		g_value_init(message, G_TYPE_STRING);

		g_value_set_string(message, cp->status_message);

		parameters = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)(bastard_destroyer_from_collabora));

		g_hash_table_insert(parameters, "message", message);

		contact_status = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)(g_hash_table_destroy));

		g_hash_table_insert(contact_status, (gpointer)(idle_statuses[cp->presence_state].name), parameters);

		vals = g_value_array_new(2);

		g_value_array_append(vals, NULL);
		g_value_init(g_value_array_get_nth(vals, 0), G_TYPE_UINT);
		g_value_set_uint(g_value_array_get_nth(vals, 0), cp->last_activity);

		g_value_array_append(vals, NULL);
		g_value_init(g_value_array_get_nth(vals, 1),
				dbus_g_type_get_map("GHashTable", G_TYPE_STRING,
					dbus_g_type_get_map("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)));
		g_value_take_boxed(g_value_array_get_nth(vals, 1), contact_status);

		g_hash_table_insert(presence, GINT_TO_POINTER(contact_handles[i]), vals);
	}
	
	g_debug("%s: emitting PRESENCE_UPDATE with %u presences", G_STRFUNC, g_hash_table_size(presence));
	g_signal_emit(self, signals[PRESENCE_UPDATE], 0, presence);
	g_hash_table_destroy(presence);
}

static gboolean signal_own_presence (IdleConnection *self, GError **error)
{
	IdleConnectionPrivate *priv;
	gchar msg[IRC_MSG_MAXLEN+1];
	IdleContactPresence *cp;

	g_assert(self != NULL);
	g_assert(error != NULL);
	g_assert(IDLE_IS_CONNECTION(self));

	priv = IDLE_CONNECTION_GET_PRIVATE(self);
	
	cp = idle_handle_get_presence(self->handles[TP_HANDLE_TYPE_CONTACT], priv->self_handle);

	g_assert(cp != NULL);
	
	switch (cp->presence_state)
	{
		case IDLE_PRESENCE_AVAILABLE:
		{
			strcpy(msg, "AWAY");
		}
		break;
		default:
		{
			gchar *awaymsg;

			/* we can't post a zero-length away message so let's use "away" instead */
			if ((cp->status_message == NULL) || (strlen(cp->status_message) == 0))
			{
				awaymsg = g_strdup("away");
			}
			else
			{
				awaymsg = g_strndup(cp->status_message, IRC_MSG_MAXLEN+1-strlen("AWAY :"));
			}
			
			sprintf(msg, "AWAY :%s", awaymsg);

			g_free(awaymsg);
		}
		break;
	}

	send_irc_cmd(self, msg);

	return TRUE;
}

static IdleIMChannel *new_im_channel(IdleConnection *conn, TpHandle handle, gboolean suppress_handler)
{
	IdleConnectionPrivate *priv;
	IdleIMChannel *chan;
	gchar *object_path;

	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	object_path = g_strdup_printf("%s/ImChannel%u", priv->object_path, handle);

	chan = g_object_new(IDLE_TYPE_IM_CHANNEL, "connection", conn,
												"object-path", object_path,
												"handle", handle,
												NULL);

	g_debug("%s: object path %s", G_STRFUNC, object_path);

	g_signal_connect(chan, "closed", (GCallback)(im_channel_closed_cb), conn);

	g_hash_table_insert(priv->im_channels, GINT_TO_POINTER(handle), chan);
	
	g_signal_emit(conn, signals[NEW_CHANNEL], 0, object_path, TP_IFACE_CHANNEL_TYPE_TEXT,
					TP_HANDLE_TYPE_CONTACT, handle, suppress_handler);

	g_free(object_path);

	return chan;
}

static IdleMUCChannel *new_muc_channel(IdleConnection *conn, TpHandle handle, gboolean suppress_handler)
{
	IdleConnectionPrivate *priv;
	IdleMUCChannel *chan;
	gchar *object_path;

	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	object_path = g_strdup_printf("%s/MucChannel%u", priv->object_path, handle);

	chan = g_object_new(IDLE_TYPE_MUC_CHANNEL, "connection", conn,
												"object-path", object_path,
												"handle", handle,
												NULL);

	g_debug("%s: object path %s", G_STRFUNC, object_path);

	g_signal_connect(chan, "closed", (GCallback)(muc_channel_closed_cb), conn);

	g_hash_table_insert(priv->muc_channels, GINT_TO_POINTER(handle), chan);

	g_signal_emit(conn, signals[NEW_CHANNEL], 0, object_path, TP_IFACE_CHANNEL_TYPE_TEXT,
					TP_HANDLE_TYPE_ROOM, handle, suppress_handler);
	
	g_free(object_path);

	return chan;
}

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

static IdleMUCChannel *new_muc_channel_async_req(IdleConnection *conn, TpHandle handle, gboolean suppress_handler, DBusGMethodInvocation *ctx)
{
	IdleConnectionPrivate *priv;
	IdleMUCChannel *chan;
	gchar *object_path;
	chan_req_data *req_data;

	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));

	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	object_path = g_strdup_printf("%s/MucChannel%u", priv->object_path, handle);

	chan = g_object_new(IDLE_TYPE_MUC_CHANNEL, "connection", conn,
												"object-path", object_path,
												"handle", handle,
												NULL);

	g_debug("%s: object path %s", G_STRFUNC, object_path);

	g_signal_connect(chan, "closed", (GCallback)(muc_channel_closed_cb), conn);
	g_signal_connect(chan, "join-ready", (GCallback)(muc_channel_join_ready_cb), conn);

	g_hash_table_insert(priv->muc_channels, GINT_TO_POINTER(handle), chan);

	req_data = g_new0(chan_req_data, 1);

	req_data->ctx = ctx;
	req_data->suppress_handler = suppress_handler;

	g_hash_table_insert(priv->chan_req_ctxs, chan, req_data);

	_idle_muc_channel_join_attempt(chan);

	g_free(object_path);

	return chan;
}

static void im_channel_closed_cb(IdleIMChannel *chan, gpointer user_data)
{
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	IdleConnectionPrivate *priv;
	TpHandle contact_handle;

	g_assert(conn != NULL);
	g_assert(IDLE_IS_CONNECTION(conn));
	
	priv = IDLE_CONNECTION_GET_PRIVATE(conn);

	if (priv->im_channels)
	{
		g_object_get(chan, "handle", &contact_handle, NULL);

		g_debug("%s: removing channel with handle %u", G_STRFUNC, contact_handle);
		g_hash_table_remove(priv->im_channels, GINT_TO_POINTER(contact_handle));
		g_debug("%s: removed channel with handle %u", G_STRFUNC, contact_handle);
	}
}

static void muc_channel_closed_cb(IdleMUCChannel *chan, gpointer user_data)
{
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	TpHandle room_handle;

	if (priv->muc_channels)
	{
		g_object_get(chan, "handle", &room_handle, NULL);
	
		g_debug("%s: removing channel with handle %u", G_STRFUNC, room_handle);
		g_hash_table_remove(priv->muc_channels, GINT_TO_POINTER(room_handle));
		g_debug("%s: removed channel with handle %u", G_STRFUNC, room_handle);
	}
}

static GHashTable *
get_statuses_arguments()
{
  static GHashTable *arguments = NULL;

  if (arguments == NULL)
    {
      arguments = g_hash_table_new (g_str_hash, g_str_equal);

      g_hash_table_insert (arguments, "message", "s");
    }

  return arguments;
}

/* D-BUS-exported methods */

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
	IdleConnectionPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_CONNECTION(obj));
	
	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	ERROR_IF_NOT_CONNECTED(obj, priv, *error);

	*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED, 
			"Only one status is possible at a time with this protocol");
	
  	return FALSE;
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
	IdleConnectionPrivate *priv;
	IdleContactPresence *cp;
	
	g_assert(obj != NULL);
	g_assert(IDLE_IS_CONNECTION(obj));

	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	ERROR_IF_NOT_CONNECTED(obj, priv, *error);

	cp = idle_handle_get_presence(obj->handles[TP_HANDLE_TYPE_CONTACT], priv->self_handle);

    if (!cp)
    {
      return TRUE;
    }

	cp->presence_state = IDLE_PRESENCE_AVAILABLE;
	g_free(cp->status_message);
	cp->status_message = NULL;
	cp->last_activity = 0;

  	return signal_own_presence(obj, error);
}


/**
 * idle_connection_connect
 *
 * Implements DBus method Connect
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_connect (IdleConnection *obj, GError **error)
{
	g_assert(obj != NULL);
  	g_assert(IDLE_IS_CONNECTION(obj));

	return _idle_connection_connect(obj, error);
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
	g_assert(obj != NULL);
  	g_assert(IDLE_IS_CONNECTION(obj));

	connection_disconnect(obj, TP_CONNECTION_STATUS_REASON_REQUESTED);
  	
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
	const char *interfaces[] = {TP_IFACE_CONNECTION_INTERFACE_PRESENCE, TP_IFACE_CONNECTION_INTERFACE_RENAMING,	NULL};
	IdleConnectionPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_CONNECTION(obj));

	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	ERROR_IF_NOT_CONNECTED(obj, priv, *error);

	*ret = g_strdupv((gchar**)(interfaces));
	
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
	IdleConnectionPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_CONNECTION(obj));

	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	*ret = g_strdup(priv->protocol);
	
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
  	IdleConnectionPrivate *priv;

  	g_assert(IDLE_IS_CONNECTION(obj));

  	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

  	ERROR_IF_NOT_CONNECTED(obj, priv, *error);

  	*ret = priv->self_handle;
  	
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
	IdleConnectionPrivate *priv;

	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	*ret = priv->status;

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
  	IdleConnectionPrivate *priv;
  	GValueArray *status;
  	int i;

  	g_assert (IDLE_IS_CONNECTION (obj));

  	priv = IDLE_CONNECTION_GET_PRIVATE (obj);

  	ERROR_IF_NOT_CONNECTED (obj, priv, *error)

  	g_debug ("%s called.", G_STRFUNC);

  	*ret = g_hash_table_new_full (g_str_hash, g_str_equal,
                                NULL, (GDestroyNotify) g_value_array_free);

  	for (i=0; i < LAST_IDLE_PRESENCE_ENUM; i++)
    {
    	status = g_value_array_new (5);

      	g_value_array_append (status, NULL);
      	g_value_init (g_value_array_get_nth (status, 0), G_TYPE_UINT);
      	g_value_set_uint (g_value_array_get_nth (status, 0),
          						idle_statuses[i].presence_type);

      	g_value_array_append (status, NULL);
      	g_value_init (g_value_array_get_nth(status, 1), G_TYPE_BOOLEAN);
      	g_value_set_boolean(g_value_array_get_nth(status, 1),
          						idle_statuses[i].self);

      	g_value_array_append (status, NULL);
      	g_value_init(g_value_array_get_nth (status, 2), G_TYPE_BOOLEAN);
      	g_value_set_boolean(g_value_array_get_nth (status, 2),
          						idle_statuses[i].exclusive);

      	g_value_array_append (status, NULL);
      	g_value_init (g_value_array_get_nth (status, 3),
          						DBUS_TYPE_G_STRING_STRING_HASHTABLE);
      	g_value_set_static_boxed(g_value_array_get_nth (status, 3),
          						get_statuses_arguments());

      	g_hash_table_insert (*ret, (gchar*)(idle_statuses[i].name), status);
    }
    
  	return TRUE;
}


/**
 * idle_connection_hold_handles
 *
 * Implements DBus method HoldHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean idle_connection_hold_handles (IdleConnection *obj,
									   guint handle_type,
									   const GArray *handles,
									   DBusGMethodInvocation *context)
{
	IdleConnectionPrivate *priv;
	GError *error = NULL;
	gchar *sender;
	int i;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_CONNECTION(obj));

	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	ERROR_IF_NOT_CONNECTED_ASYNC(obj, priv, error, context);

	if (!tp_handle_type_is_valid(handle_type, NULL))
	{
		g_debug("%s: invalid handle type %u", G_STRFUNC, handle_type);
		
		error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "invalid handle type %u", handle_type);
		dbus_g_method_return_error(context, error);
		g_error_free(error);

		return FALSE;
	}

	sender = dbus_g_method_get_sender(context);

	for (i=0; i<handles->len; i++)
	{
		TpHandle handle = g_array_index(handles, guint, i);
    if (!tp_handle_client_hold(obj->handles[handle_type], sender, handle, &error))
    {
      dbus_g_method_return_error(context, error);
      g_error_free(error);
      return FALSE;
    }
	}

	dbus_g_method_return(context);

  free(sender);
		
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
gboolean idle_connection_inspect_handle (IdleConnection *obj, guint handle_type, guint handle, gchar ** ret, GError **_error)
{
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(obj);
	const gchar *tmp;

	ERROR_IF_NOT_CONNECTED(obj, priv, *_error);

	if (!tp_handle_type_is_valid(handle_type, NULL))
	{
		g_debug("%s: invalid handle type %u", G_STRFUNC, handle_type);

		*_error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "invalid handle type %u", handle_type);

		return FALSE;
	}

	tmp = idle_handle_inspect(obj->handles[handle_type], handle);

	if (tmp == NULL)
	{
		g_debug("%s: invalid handle %u (type %u)", G_STRFUNC, handle, handle_type);

		*_error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_HANDLE, "invalid handle %u", handle);

		return FALSE;
	}

	*ret = g_strdup(tmp);

	return TRUE;
}

gboolean idle_connection_inspect_handles (IdleConnection *conn, guint handle_type, const GArray *handles, DBusGMethodInvocation *ctx)
{
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	const char *tmp;
	const gchar **ret;
	int i;
	GError *error;

	ERROR_IF_NOT_CONNECTED_ASYNC(obj, priv, error, ctx);

	if (!tp_handle_type_is_valid(handle_type, NULL))
	{
		g_debug("%s: invalid handle type %u", G_STRFUNC, handle_type);

		error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "invalid handle type %u", handle_type);

		dbus_g_method_return_error(ctx, error);

		return FALSE;
	}

	ret = g_new(const gchar *, handles->len+1);

	for (i=0; i<handles->len; i++)
	{
		TpHandle handle = g_array_index(handles, guint, i);
		
		tmp = idle_handle_inspect(conn->handles[handle_type], handle);

		if (tmp == NULL)
		{
			g_debug("%s: invalid handle %u (type %u)", G_STRFUNC, handle, handle_type);

			error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_HANDLE, "invalid handle %u", handle);

			g_free(ret);

			dbus_g_method_return_error(ctx, error);
			
			return FALSE;
		}

		ret[i] = tmp;
	}

	ret[i] = NULL;

	dbus_g_method_return(ctx, ret);

	g_free(ret);
	
	return TRUE;
}

/**
 * list_channel_hash_foreach:
 * @key: iterated key
 * @value: iterated value
 * @data: data attached to this key/value pair
 *
 * Called by the exported ListChannels function, this should iterate over
 * the handle/channel pairs in a hash, and to the GPtrArray in the
 * ListChannelInfo struct, add a GValueArray containing the following:
 *  a D-Bus object path for the channel object on this service
 *  a D-Bus interface name representing the channel type
 *  an integer representing the handle type this channel communicates with, or zero
 *  an integer handle representing the contact, room or list this channel communicates with, or zero
 */
static void
list_channel_hash_foreach (gpointer key,
                           gpointer value,
                           gpointer data)
{
  GObject *channel = G_OBJECT (value);
  GPtrArray *channels = (GPtrArray *) data;
  char *path, *type;
  guint handle_type, handle;
  GValueArray *vals;

  g_object_get (channel, "object-path", &path,
                         "channel-type", &type,
                         "handle-type", &handle_type,
                         "handle", &handle, NULL);

  g_debug ("list_channels_foreach_hash: adding path %s, type %s, "
           "handle type %u, handle %u", path, type, handle_type, handle);

  vals = g_value_array_new (4);

  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 0), DBUS_TYPE_G_OBJECT_PATH);
  g_value_set_boxed (g_value_array_get_nth (vals, 0), path);
  g_free (path);

  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 1), G_TYPE_STRING);
  g_value_set_string (g_value_array_get_nth (vals, 1), type);
  g_free (type);

  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 2), G_TYPE_UINT);
  g_value_set_uint (g_value_array_get_nth (vals, 2), handle_type);

  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 3), G_TYPE_UINT);
  g_value_set_uint (g_value_array_get_nth (vals, 3), handle);

  g_ptr_array_add (channels, vals);
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
	IdleConnectionPrivate *priv;
	guint count;
	GPtrArray *channels;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_CONNECTION(obj));
	
	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	ERROR_IF_NOT_CONNECTED(obj, priv, *error);

	count = g_hash_table_size(priv->im_channels);
	count += g_hash_table_size(priv->muc_channels);
	channels = g_ptr_array_sized_new(count);

	g_hash_table_foreach(priv->im_channels, list_channel_hash_foreach, channels);
	g_hash_table_foreach(priv->muc_channels, list_channel_hash_foreach, channels);

	*ret = channels;

	return TRUE;
}


/**
 * idle_connection_release_handles
 *
 * Implements DBus method ReleaseHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean idle_connection_release_handles (IdleConnection *obj,
										  guint handle_type,
										  const GArray *handles,
										  DBusGMethodInvocation *context)
{
	IdleConnectionPrivate *priv;
	char *sender;
	GError *error = NULL;
	int i;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_CONNECTION(obj));
	
	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	ERROR_IF_NOT_CONNECTED_ASYNC(obj, priv, error, context);

	if (!tp_handle_type_is_valid(handle_type, NULL))
	{
		g_debug("%s: invalid handle type %u", G_STRFUNC, handle_type);

		error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "invalid handle type %u", handle_type);

		dbus_g_method_return_error(context, error);

		g_error_free(error);

		return FALSE;
	}

	sender = dbus_g_method_get_sender(context);
	
	for (i=0; i<handles->len; i++)
	{
		TpHandle handle = g_array_index(handles, guint, i);

		if (!tp_handle_client_release(obj->handles[handle_type], sender, handle, &error))
		{
			dbus_g_method_return_error(context, error);
			g_error_free(error);
			return FALSE;
		}
	}

	dbus_g_method_return(context);

  free(sender);

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
	IdleConnectionPrivate *priv;
	IdleContactPresence *cp;

	g_assert(IDLE_IS_CONNECTION(obj));

	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	ERROR_IF_NOT_CONNECTED(obj, priv, *error);

	cp = idle_handle_get_presence(obj->handles[TP_HANDLE_TYPE_CONTACT], priv->self_handle);

	if ((cp != NULL) && !strcmp(status, idle_statuses[cp->presence_state].name))
	{
		cp->presence_state = IDLE_PRESENCE_AVAILABLE;
		g_free(cp->status_message);
		cp->status_message = NULL;
		cp->last_activity = 0;
		return signal_own_presence(obj, error);
	}
	else
	{
		*error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Attempting to remove non-existant presence.");
		return FALSE;
	}
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
gboolean idle_connection_request_channel (IdleConnection *obj, const gchar * type, guint handle_type, guint handle, gboolean suppress_handler, DBusGMethodInvocation *ctx)
{
	IdleConnectionPrivate *priv;
	GError *error = NULL;
	gboolean queued = FALSE;
	gchar *ret;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_CONNECTION(obj));

	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	ERROR_IF_NOT_CONNECTED_ASYNC(obj, priv, error, ctx);

	if (!tp_handle_is_valid(obj->handles[handle_type], handle, NULL))
	{
		goto INVALID_HANDLE;
	}

	if (!strcmp(type, TP_IFACE_CHANNEL_TYPE_TEXT))
	{
		gpointer chan;

		switch (handle_type)
		{
			case TP_HANDLE_TYPE_CONTACT:
			{
				chan = g_hash_table_lookup(priv->im_channels, GINT_TO_POINTER(handle));
			}
			break;
			case TP_HANDLE_TYPE_ROOM:
			{
				chan = g_hash_table_lookup(priv->muc_channels, GINT_TO_POINTER(handle));
			}
			break;
			default:
			{
				goto NOT_AVAILABLE;
			}
			break;
		}

		if (chan == NULL)
		{
			switch (handle_type)
			{
				case TP_HANDLE_TYPE_CONTACT:
				{
					chan = new_im_channel(obj, handle, suppress_handler);
				}
				break;
				case TP_HANDLE_TYPE_ROOM:
				{
					queued = TRUE;
					chan = new_muc_channel_async_req(obj, handle, suppress_handler, ctx);
				}
				break;
				default:
				{
					goto NOT_AVAILABLE;
				}
				break;
			}
		}

		g_object_get(chan, "object-path", &ret, NULL);
	}
	else
	{
		goto NOT_IMPLEMENTED;
	}

	if (!queued)
	{
		dbus_g_method_return(ctx, ret);
	}

	g_free(ret);
	
	return TRUE;

NOT_AVAILABLE:
  g_debug ("request_channel: requested channel is unavailable with "
           "handle type %u", handle_type);

  error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                        "requested channel is not available with "
                        "handle type %u", handle_type);
  dbus_g_method_return_error(ctx, error);
  g_free(error);

  return FALSE;

INVALID_HANDLE:
  g_debug ("request_channel: handle %u (type %u) not valid", handle, handle_type);

  error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_HANDLE,
                        "handle %u (type %u) not valid", handle, handle_type);
  dbus_g_method_return_error(ctx, error);
  g_free(error);

  return FALSE;

NOT_IMPLEMENTED:
  g_debug ("request_channel: unsupported channel type %s", type);

  error = g_error_new (TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
                        "unsupported channel type %s", type);
  dbus_g_method_return_error(ctx, error);
  g_free(error);

  return FALSE;
}

static TpHandle _idle_connection_request_handle(IdleConnection *obj,
												  guint handle_type,
												  const gchar *name,
												  GError **error);

/**
 * idle_connection_request_handles
 *
 * Implements DBus method RequestHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean idle_connection_request_handles (IdleConnection *obj,
										  guint handle_type,
										  const gchar **names,
										  DBusGMethodInvocation *context)
{
	IdleConnectionPrivate *priv;
	gchar *sender;
	GError *error = NULL;
	GArray *handles;
	const gchar **name;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_CONNECTION(obj));

	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	ERROR_IF_NOT_CONNECTED_ASYNC(obj, priv, error, context);

	if (!tp_handle_type_is_valid(handle_type, NULL))
	{
		g_debug("%s: invalid handle type %u", G_STRFUNC, handle_type);

		error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "invalid handle type %u", handle_type);
		dbus_g_method_return_error(context, error);
		g_error_free(error);

		return FALSE;
	}

	sender = dbus_g_method_get_sender(context);
	handles = g_array_new(FALSE, FALSE, sizeof(TpHandle));

	for (name = names; *name != NULL; name++)
	{
		TpHandle handle;

		handle = _idle_connection_request_handle(obj, handle_type, *name, &error);

		if (!handle)
		{
			g_debug("%s: failed to request handle: %s", G_STRFUNC, error->message);
			g_array_free(handles, TRUE);

			dbus_g_method_return_error(context, error);
			g_error_free(error);

			return FALSE;
		}

    if (!tp_handle_client_hold(obj->handles[handle_type], sender, handle, &error))
    {
      dbus_g_method_return_error(context, error);
      g_error_free(error);
      return FALSE;
    }

		g_array_append_val(handles, handle);
	}
	
	dbus_g_method_return(context, handles);
	g_array_free(handles, TRUE);

	return TRUE;
}

static TpHandle _idle_connection_request_handle(IdleConnection *obj,
												  guint handle_type,
												  const gchar *name,
												  GError **error)
{
	TpHandle handle;
	gchar *final_name;
	
	switch (handle_type)
	{
		case TP_HANDLE_TYPE_CONTACT:
		{
			handle = idle_handle_for_contact(obj->handles[TP_HANDLE_TYPE_CONTACT], name);
		}
		break;
		case TP_HANDLE_TYPE_ROOM:
		{
			switch (name[0])
			{
				case '#':
				case '&':
				case '!':
				case '+':
				{
					final_name = (gchar *)(name);
				}
				break;
				default:
				{
					g_debug("%s: assuming user wanted #-channel (%s)", G_STRFUNC, name);
					final_name = g_strdup_printf("#%s", name);
				}
				break;
			}
			
			handle = idle_handle_for_room(obj->handles[TP_HANDLE_TYPE_ROOM], final_name);
			
			if (final_name != name)
			{
				g_free(final_name);
			}
		}
		break;
		default:
		{
			g_debug("%s: unimplemented handle type %u", G_STRFUNC, handle_type);

			*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "unimplemented handle type %u", handle_type);
			return 0;
		}
		break;
	}

	if (handle == 0)
	{
		g_debug("%s: requested name %s was invalid", G_STRFUNC, name);

		*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "requested name %s was invalid", name);

		return 0;
	}

	return handle;
}

static gboolean presence_timer_cb(gpointer data)
{
	IdleConnection *conn = IDLE_CONNECTION(data);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(conn);
	gchar *nick;
	gchar cmd[IRC_MSG_MAXLEN+2];

	nick = g_queue_pop_head(priv->presence_queue);

	if (nick == NULL)
	{
		priv->presence_unload_timer_id = 0;

		return FALSE;
	}
	
	g_snprintf(cmd, IRC_MSG_MAXLEN+2, "WHOIS %s", nick);

	send_irc_cmd_full(conn, cmd, SERVER_CMD_MIN_PRIORITY);

	priv->presence_reply_list = g_list_append(priv->presence_reply_list, nick);

	return TRUE;
}

#define PRESENCE_TIMER_TIMEOUT 3300

static void presence_request_push(IdleConnection *obj, const gchar *nick)
{
	IdleConnectionPrivate *priv;
	GList *node;
	
	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	node = g_queue_find_custom(priv->presence_queue, nick, (GCompareFunc)(strcmp));

	if (!node)
	{
		g_queue_push_tail(priv->presence_queue, g_strdup(nick));
	}

	if (priv->presence_unload_timer_id == 0)
	{
		priv->presence_unload_timer_id = g_timeout_add(PRESENCE_TIMER_TIMEOUT, presence_timer_cb, obj);
	}
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
	IdleConnectionPrivate *priv;
	int i;
	TpHandle *handles = g_new0(TpHandle, contacts->len+1);

	g_assert(obj != NULL);
	g_assert(IDLE_IS_CONNECTION(obj));

	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	ERROR_IF_NOT_CONNECTED(obj, priv, *error);

	for (i=0; i<contacts->len; i++)
	{
		TpHandle handle;

		handle = g_array_index(contacts, guint, i);

		if (!tp_handle_is_valid(obj->handles[TP_HANDLE_TYPE_CONTACT], handle, NULL))
		{
			g_debug("%s: invalid handle %u", G_STRFUNC, handle);

			*error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_HANDLE, "invalid handle %u", handle);

			return FALSE;
		}

		handles[i] = handle;
	}

	handles[i] = 0;

	g_debug("%s: got presence request for %u handles", G_STRFUNC, contacts->len);

	if (contacts->len)
	{
		emit_presence_update(obj, handles);
	}

	g_free(handles);

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
	g_assert(obj != NULL);
	g_assert(IDLE_IS_CONNECTION(obj));

	ERROR_IF_NOT_CONNECTED(obj, IDLE_CONNECTION_GET_PRIVATE(obj), *error);
	
	/* this is not really possible but let's pretend it succeeded */
	
	return TRUE;
}

struct idle_conn_hashtable_foreach_data
{
	IdleConnection *conn;
	GError **error;
	gboolean retval;
};

static void setstatuses_foreach(gpointer key, gpointer value, gpointer user_data)
{
	struct idle_conn_hashtable_foreach_data *data = (struct idle_conn_hashtable_foreach_data *)(user_data);
	IdleConnectionPrivate *priv = IDLE_CONNECTION_GET_PRIVATE(data->conn);
	int i;

	for (i = 0; i < LAST_IDLE_PRESENCE_ENUM; i++)
	{
		if (!strcmp(idle_statuses[i].name, (const gchar *)(key)))
		{
			break;
		}
	}

	if (i < LAST_IDLE_PRESENCE_ENUM)
	{
		GHashTable *args = (GHashTable *)(value);
		GValue *message = g_hash_table_lookup(args, "message");
		const gchar *status = NULL;
		IdleContactPresence *cp;

		if (message)
		{
			if (!G_VALUE_HOLDS_STRING(message))
			{
				g_debug("%s: got a status message which was not a string", G_STRFUNC);
				
				if (*(data->error))
				{
					g_error_free(*(data->error));
					*(data->error) = NULL;
				}

				*(data->error) = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
								"Status argument 'message' requires a string");
				data->retval = FALSE;
				return;
			}

			status = g_value_get_string(message);
		}

		cp = idle_handle_get_presence(data->conn->handles[TP_HANDLE_TYPE_CONTACT], priv->self_handle);

		if (!cp)
		{
			cp = idle_contact_presence_new();
			g_assert(idle_handle_set_presence(data->conn->handles[TP_HANDLE_TYPE_CONTACT], priv->self_handle, cp));
		}

		cp->presence_state = i;
		cp->status_message = g_strdup(status);
		cp->last_activity = 0;

		data->retval = signal_own_presence(data->conn, data->error);
	}
	else
	{
		g_debug("%s: got unknown status identifier %s", G_STRFUNC, (const gchar *)(key));
		
		if (*(data->error))
		{
			g_error_free(*(data->error));
			*(data->error) = NULL;
		}
		
		*(data->error) = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
									"unknown status identifier received: %s",
									(const gchar *)(key));
		data->retval = FALSE;
	}
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
	IdleConnectionPrivate *priv;
	int size;
	struct idle_conn_hashtable_foreach_data data = {obj, error, TRUE};
	
	g_assert(obj != NULL);
	g_assert(IDLE_IS_CONNECTION(obj));

	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	ERROR_IF_NOT_CONNECTED(obj, priv, *error);
	
	if ((size = g_hash_table_size(statuses)) != 1)
	{
		g_debug("%s: got %i statuses instead of 1", G_STRFUNC, size);
		
		*error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Got %i statuses instead of 1", size);
		
		return FALSE;
	}

	g_hash_table_foreach(statuses, setstatuses_foreach, &data);

	if (*(data.error) != NULL)
	{
		g_debug("%s: error: %s", G_STRFUNC, (*data.error)->message);
		g_error_free(*(data.error));
	}
	
	return data.retval;
}

gboolean idle_connection_request_rename(IdleConnection *obj, const gchar *nick, GError **error)
{
	IdleConnectionPrivate *priv;
	TpHandle handle;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_CONNECTION(obj));

	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	handle = idle_handle_for_contact(obj->handles[TP_HANDLE_TYPE_CONTACT], nick);

	if (handle == 0)
	{
		g_debug("%s: failed to get handle for (%s)", G_STRFUNC, nick);

		*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "Failed to get handle for (%s)", nick);

		return FALSE;
	}

	g_free(priv->nickname);

	priv->nickname = g_strdup(nick);

	priv_rename(obj, priv->self_handle, handle);

	return TRUE;
}

gboolean idle_connection_hton(IdleConnection *obj, const gchar *input, gchar **output, GError **_error)
{
	IdleConnectionPrivate *priv;
	GError *error = NULL;
	gsize bytes_written;
	gchar *ret;

	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	if (input == NULL)
	{
		*output = NULL;
		return TRUE;
	}

	ret = g_convert(input, -1, priv->charset, "UTF-8", NULL, &bytes_written, &error);

	if (ret == NULL)
	{
		g_debug("%s: g_convert failed: %s", G_STRFUNC, error->message);
		*_error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "character set conversion failed: %s", error->message);
		g_error_free(error);
		*output = NULL;
		return FALSE;
	}
	
	*output = ret;
	return TRUE;
}

gboolean idle_connection_ntoh(IdleConnection *obj, const gchar *input, gchar **output, GError **_error)
{
	IdleConnectionPrivate *priv;
	GError *error = NULL;
	gsize bytes_written;
	gchar *ret;
	gchar *p;

	priv = IDLE_CONNECTION_GET_PRIVATE(obj);

	if (input == NULL)
	{
		*output = NULL;
		return TRUE;
	}

	ret = g_convert(input, -1, "UTF-8", priv->charset, NULL, &bytes_written, &error);

	if (ret == NULL)
	{
		g_debug("%s: charset conversion failed, falling back to US-ASCII: %s", G_STRFUNC, error->message);
		g_error_free(error);

		ret = g_strdup(input);

		for (p=ret; *p != '\0'; p++)
		{
			if (*p & (1<<7))
			{
				*p = '?';
			}
		}
	}
	
	*output = ret;
	return TRUE;
}

