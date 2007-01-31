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

#include <dbus/dbus-glib.h>
#include <dbus/dbus-protocol.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "idle-connection.h"
#include "telepathy-helpers.h"

#include "idle-connection-manager.h"
#include "idle-connection-manager-glue.h"
#include "idle-connection-manager-signals-marshal.h"

#define BUS_NAME	"org.freedesktop.Telepathy.ConnectionManager.idle"
#define OBJECT_PATH	"/org/freedesktop/Telepathy/ConnectionManager/idle"

G_DEFINE_TYPE(IdleConnectionManager, idle_connection_manager, G_TYPE_OBJECT)

/* signal enum */
enum
{
    NEW_CONNECTION,
    NO_MORE_CONNECTIONS,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _IdleConnectionManagerPrivate IdleConnectionManagerPrivate;

struct _IdleConnectionManagerPrivate
{
  gboolean dispose_has_run;
  GHashTable *connections;
};

#define IDLE_CONNECTION_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), IDLE_TYPE_CONNECTION_MANAGER, IdleConnectionManagerPrivate))

static void
idle_connection_manager_init (IdleConnectionManager *obj)
{
  IdleConnectionManagerPrivate *priv = IDLE_CONNECTION_MANAGER_GET_PRIVATE (obj);

  priv->connections = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void idle_connection_manager_dispose (GObject *object);
static void idle_connection_manager_finalize (GObject *object);

static void
idle_connection_manager_class_init (IdleConnectionManagerClass *idle_connection_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (idle_connection_manager_class);

  g_type_class_add_private (idle_connection_manager_class, sizeof (IdleConnectionManagerPrivate));

  object_class->dispose = idle_connection_manager_dispose;
  object_class->finalize = idle_connection_manager_finalize;

  signals[NEW_CONNECTION] =
    g_signal_new ("new-connection",
                  G_OBJECT_CLASS_TYPE (idle_connection_manager_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_connection_manager_marshal_VOID__STRING_STRING_STRING,
                  G_TYPE_NONE, 3, G_TYPE_STRING, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING);

  signals[NO_MORE_CONNECTIONS] =
    g_signal_new ("no-more-connections",
                  G_OBJECT_CLASS_TYPE (idle_connection_manager_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (idle_connection_manager_class), &dbus_glib_idle_connection_manager_object_info);
}

void
idle_connection_manager_dispose (GObject *object)
{
  IdleConnectionManager *self = IDLE_CONNECTION_MANAGER (object);
  IdleConnectionManagerPrivate *priv = IDLE_CONNECTION_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (idle_connection_manager_parent_class)->dispose)
    G_OBJECT_CLASS (idle_connection_manager_parent_class)->dispose (object);
}

void
idle_connection_manager_finalize (GObject *object)
{
  IdleConnectionManager *self = IDLE_CONNECTION_MANAGER (object);
  IdleConnectionManagerPrivate *priv = IDLE_CONNECTION_MANAGER_GET_PRIVATE (self);

  g_hash_table_destroy(priv->connections);

  G_OBJECT_CLASS (idle_connection_manager_parent_class)->finalize (object);
}

/* private data */

typedef struct _IdleParams IdleParams;

struct _IdleParams
{
	gchar *nickname;
	gchar *server;
	guint16 port;
	gchar *password;
	gchar *realname;
	gchar *charset;
	gchar *quit_message;
	gboolean use_ssl;
};

typedef struct _IdleParamSpec IdleParamSpec;

struct _IdleParamSpec
{
	const gchar *name;
	const gchar *dtype;
	const GType gtype;
	guint flags;
	const gpointer def;
	const gsize offset;
};

static const IdleParamSpec irc_params[] =
{
	{"account", 	DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL, G_STRUCT_OFFSET(IdleParams, nickname)},
	{"server", 		DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL, G_STRUCT_OFFSET(IdleParams, server)},
	{"port",		DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(6667), G_STRUCT_OFFSET(IdleParams, port)},
	{"password",	DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, G_STRUCT_OFFSET(IdleParams, password)},
	{"fullname",	DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, G_STRUCT_OFFSET(IdleParams, realname)},
	{"charset",		DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, "UTF-8", G_STRUCT_OFFSET(IdleParams, charset)},
	{"quit-message",DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, "So long and thanks for all the IRC - telepathy-idle IRC Connection Manager for Telepathy - http://telepathy.freedesktop.org", G_STRUCT_OFFSET(IdleParams, quit_message)},
	{"use-ssl",		DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE), G_STRUCT_OFFSET(IdleParams, use_ssl)},
	{NULL, NULL, 0, 0, NULL, 0}
};

/* private methods */

static gboolean get_parameters (const char *proto, const IdleParamSpec **params, GError **error)
{
	if (!strcmp(proto, "irc"))
	{
		*params = irc_params;
		
		return TRUE;
	}
	else
	{
		g_debug("%s: unknown protocol %s", G_STRFUNC, proto);

		*error = g_error_new (TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED, "unknown protocol %s", proto);

		return FALSE;
	}
}

static void set_default_param (const IdleParamSpec *paramspec, IdleParams *params)
{
  	switch (paramspec->dtype[0])
  	{
  		case DBUS_TYPE_STRING:
		{
			*((char **)((void *)params + paramspec->offset)) = g_strdup(paramspec->def);
        	break;
		}
      	case DBUS_TYPE_UINT16:
		{
        	*((guint16 *)((void *)params + paramspec->offset)) = GPOINTER_TO_INT(paramspec->def);
        	break;
		}
      	case DBUS_TYPE_BOOLEAN:
		{
        	*((gboolean *)((void *)params + paramspec->offset)) = GPOINTER_TO_INT(paramspec->def);
        	break;
		}
      	default:
		{
        	g_error("%s: unknown parameter type %s on parameter %s", G_STRFUNC, paramspec->dtype, paramspec->name);
        	break;
		}
	}
}

static gboolean set_param_from_value (const IdleParamSpec *paramspec, GValue *value, IdleParams *params, GError **error)
{
	if (G_VALUE_TYPE (value) != paramspec->gtype)
    {
    	g_debug("%s: expected type %s for parameter %s, got %s", 	G_STRFUNC, 
																	g_type_name (paramspec->gtype), 
																	paramspec->name,
               														G_VALUE_TYPE_NAME (value));
		*error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
								"expected type %s for account parameter %s, got %s",
                            	g_type_name (paramspec->gtype), paramspec->name,
                            	G_VALUE_TYPE_NAME (value));
      	return FALSE;
    }

  	switch (paramspec->dtype[0])
    {
      	case DBUS_TYPE_STRING:
		{
        	*((char **)((void *)params + paramspec->offset)) = g_value_dup_string (value);
        	break;
		}
      	case DBUS_TYPE_UINT16:
		{
        	*((guint16 *)((void *)params + paramspec->offset)) = g_value_get_uint (value);
        	break;
        }
      	case DBUS_TYPE_BOOLEAN:
		{
        	*((gboolean *)((void *)params + paramspec->offset)) = g_value_get_boolean (value);
        	break;  
      	}
      	default:
		{
        	g_error("%s: encountered unknown type %s on argument %s", G_STRFUNC, paramspec->dtype, paramspec->name);
        }
    }
    
  	return TRUE;
}

static gboolean parse_parameters (const IdleParamSpec *paramspec, GHashTable *provided, IdleParams *params, GError **error)
{
	int params_left;
	int i;

	params_left = g_hash_table_size(provided);

	for (i=0; paramspec[i].name; i++)
	{
		GValue *value;
		value = g_hash_table_lookup(provided, paramspec[i].name);

		if (!value)
		{
			if (paramspec[i].flags & TP_CONN_MGR_PARAM_FLAG_REQUIRED)
			{
				g_debug("%s: missing REQUIRED param %s", G_STRFUNC, paramspec[i].name);
				*error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, 
										"missing REQUIRED account parameter %s",
										paramspec[i].name);
				return FALSE;
			}
			else
			{
				g_debug("%s: using default value for param %s",
						G_STRFUNC, paramspec[i].name);
				set_default_param(&(paramspec[i]), params);
			}
		}
		else
		{
			if (!set_param_from_value(&(paramspec[i]), value, params, error))
			{
				return FALSE;
			}

			params_left--;
			
			if (paramspec[i].gtype == G_TYPE_STRING)
			{
				g_debug("%s: new value %s for param %s", G_STRFUNC,
						*((char **)((void *)params+paramspec[i].offset)), paramspec[i].name);
			}
			else
			{
				g_debug("%s: new value %u for param %s", G_STRFUNC,
						*((guint *)((void *)params+paramspec[i].offset)), paramspec[i].name);
					    
			}
		}
	}

	if (params_left)
	{
		g_debug("%s: %u unknown parameters given", G_STRFUNC, params_left);
		return FALSE;
	}

	return TRUE;
}

static void free_params (IdleParams *params)
{
	g_free(params->nickname);
	g_free(params->server);
	g_free(params->password);
	g_free(params->realname);
	g_free(params->charset);
	g_free(params->quit_message);
}

static void connection_disconnected_cb(IdleConnection *conn, gpointer data)
{
	IdleConnectionManager *self = IDLE_CONNECTION_MANAGER(data);
	IdleConnectionManagerPrivate *priv = IDLE_CONNECTION_MANAGER_GET_PRIVATE(self);

	g_assert(g_hash_table_lookup(priv->connections, conn));
	g_hash_table_remove(priv->connections, conn);

	g_object_unref(conn);

	g_debug("%s: unref'd connection", G_STRFUNC);

	if (!g_hash_table_size(priv->connections))
	{
		g_signal_emit(self, signals[NO_MORE_CONNECTIONS], 0);
	}
}

/* public methods */

void
_idle_connection_manager_register (IdleConnectionManager *self)
{
	DBusGConnection *bus;
	DBusGProxy *bus_proxy;
	GError *error = NULL;
	guint request_name_result;

	g_assert (IDLE_IS_CONNECTION_MANAGER (self));

	bus = tp_get_bus ();
	bus_proxy = tp_get_bus_proxy ();
	
	if (!dbus_g_proxy_call (bus_proxy, "RequestName", &error,
							G_TYPE_STRING, BUS_NAME,
							G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
							G_TYPE_INVALID,
							G_TYPE_UINT, &request_name_result,
							G_TYPE_INVALID))
	  g_error ("Failed to request bus name: %s", error->message);

	if (request_name_result == DBUS_REQUEST_NAME_REPLY_EXISTS)
	  g_error ("Failed to acquire bus name, connection manager already running?");

	dbus_g_connection_register_g_object (bus, OBJECT_PATH, G_OBJECT (self));
}

/* dbus-exported methods */

/**
 * idle_connection_manager_connect
 *
 * Implements DBus method Connect
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_manager_request_connection (IdleConnectionManager *obj,
                                                     const gchar *proto,
                                                     GHashTable *parameters,
                                                     gchar **bus_name,
                                                     gchar **object_path,
                                                     GError **error)
{
	IdleConnectionManager *self = IDLE_CONNECTION_MANAGER(obj);
	IdleConnectionManagerPrivate *priv = IDLE_CONNECTION_MANAGER_GET_PRIVATE(self);
	IdleConnection *conn;
	const IdleParamSpec *paramspec;
	IdleParams params = {NULL};

	g_assert(IDLE_IS_CONNECTION_MANAGER(self));
	
	if (!get_parameters(proto, &paramspec, error))
	{
		return FALSE;
	}

	if (!parse_parameters(paramspec, parameters, &params, error))
	{
		free_params(&params);
		return FALSE;
	}

	conn = g_object_new(IDLE_TYPE_CONNECTION,
						"protocol",		proto,
						"nickname",		params.nickname,
						"server",		params.server,
						"port",			params.port,
						"password",		params.password,
						"realname",		params.realname,
						"charset", 		params.charset,
						"quit-message", params.quit_message,
						"use-ssl",		params.use_ssl,
						NULL);

	free_params(&params);

	if (!_idle_connection_register(conn, bus_name, object_path, error))
	{
		g_debug("%s failed: %s", G_STRFUNC, (*error)->message);
		goto lerror;
	}

	g_signal_connect(conn, "disconnected", G_CALLBACK(connection_disconnected_cb), self);
	g_hash_table_insert(priv->connections, conn, GINT_TO_POINTER(TRUE));

	g_signal_emit(obj, signals[NEW_CONNECTION], 0, *bus_name, *object_path, proto);
	
  	return TRUE;

lerror:
	if (conn)
	{
		g_object_unref(G_OBJECT(conn));
	}

	return FALSE;
}

gboolean idle_connection_manager_get_parameters (IdleConnectionManager *obj,
                                                 const gchar *proto,
                                                 GPtrArray **ret,
                                                 GError **error)
{
	const IdleParamSpec *params = NULL;
	const IdleParamSpec *param;

	if (!get_parameters(proto, &params, error))
	{
		return FALSE;
	}

	*ret = g_ptr_array_new();

	for (param = params; param->name != NULL; param++)
	{
		GValue tp_param = {0, };
		GValue *def;

		def = g_new0(GValue, 1);
		g_value_init(def, param->gtype);
		
		g_value_init(&tp_param, dbus_g_type_get_struct ("GValueArray",
														G_TYPE_STRING,
														G_TYPE_UINT,
														G_TYPE_STRING,
														G_TYPE_VALUE,
														G_TYPE_INVALID));
		
		g_value_set_static_boxed(&tp_param,
								 dbus_g_type_specialized_construct(
									 dbus_g_type_get_struct ("GValueArray",
															 G_TYPE_STRING,
															 G_TYPE_UINT,
															 G_TYPE_STRING,
															 G_TYPE_VALUE,
															 G_TYPE_INVALID)));

		if (param->def)
		{
			switch (param->gtype)
			{
				case G_TYPE_STRING:
				{
					g_value_set_static_string(def, param->def);
				}
				break;
				case G_TYPE_UINT:
				{
					g_value_set_uint(def, GPOINTER_TO_UINT(param->def));
				}
				break;
				case G_TYPE_BOOLEAN:
				{
					g_value_set_boolean(def, GPOINTER_TO_UINT(param->def));
				}
				break;
				default:
				{
					g_assert_not_reached();
				}
				break;
			}
		}
		
		dbus_g_type_struct_set(&tp_param,
							   0, param->name,
							   1, param->flags,
							   2, param->dtype,
							   3, def,
							   G_MAXUINT);

		g_value_unset(def);
		g_free(def);

		g_ptr_array_add(*ret, g_value_get_boxed(&tp_param));
	}

	return TRUE;
}

/**
 * idle_connection_manager_list_protocols
 *
 * Implements DBus method ListProtocols
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_connection_manager_list_protocols (IdleConnectionManager *obj,
                                                 gchar *** ret,
                                                 GError **error)
{
	const char *protocols[] = {"irc", NULL};

	*ret = g_strdupv((gchar **)(protocols));
	
	return TRUE;
}

