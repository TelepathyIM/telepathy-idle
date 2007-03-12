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

#include <glib.h>
#include <dbus/dbus-glib.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/dbus.h>

#include <string.h>
#include <time.h>

#include "idle-connection.h"
#include "idle-handles.h"

#include "idle-im-channel.h"
#include "idle-im-channel-glue.h"
#include "idle-im-channel-signals-marshal.h"

#define IRC_MSG_MAXLEN 510

static void text_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(IdleIMChannel, idle_im_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL_TYPE_TEXT, text_iface_init);)

/* signal enum */
enum
{
    CLOSED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* property enum */
enum
{
	PROP_CONNECTION = 1,
	PROP_OBJECT_PATH,
	PROP_CHANNEL_TYPE,
	PROP_HANDLE_TYPE,
	PROP_HANDLE,
	LAST_PROPERTY_ENUM
};

/* private structure */
typedef struct _IdleIMChannelPrivate IdleIMChannelPrivate;

struct _IdleIMChannelPrivate
{
	IdleConnection *connection;
	gchar *object_path;
	TpHandle handle;

	gboolean closed;

	gboolean dispose_has_run;
};

#define IDLE_IM_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), IDLE_TYPE_IM_CHANNEL, IdleIMChannelPrivate))

static void
idle_im_channel_init (IdleIMChannel *obj)
{
}

static void idle_im_channel_dispose (GObject *object);
static void idle_im_channel_finalize (GObject *object);

static GObject *idle_im_channel_constructor(GType type, guint n_props, GObjectConstructParam *props)
{
	GObject *obj;
	IdleIMChannelPrivate *priv;
	DBusGConnection *bus;
	TpHandleRepoIface *handles;
	gboolean valid;

	obj = G_OBJECT_CLASS(idle_im_channel_parent_class)->constructor(type, n_props, props);
	priv = IDLE_IM_CHANNEL_GET_PRIVATE(IDLE_IM_CHANNEL(obj));

	handles = priv->connection->handles[TP_HANDLE_TYPE_CONTACT];
	valid = tp_handle_ref(handles, priv->handle);
	g_assert(valid);

	bus = tp_get_bus();
	dbus_g_connection_register_g_object(bus, priv->object_path, obj);

  tp_text_mixin_init(obj, G_STRUCT_OFFSET(IdleIMChannel, text), handles, 0);
  tp_text_mixin_set_message_types(obj,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
      G_MAXUINT);

	return obj;
}

static void idle_im_channel_get_property(GObject *object, guint property_id,
										GValue *value, GParamSpec *pspec)
{	
	IdleIMChannel *chan;
	IdleIMChannelPrivate *priv;

	g_assert(object != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(object));
	
	chan = IDLE_IM_CHANNEL(object);
	priv = IDLE_IM_CHANNEL_GET_PRIVATE(chan);

	switch (property_id)
	{
		case PROP_CONNECTION:
		{
			g_value_set_object(value, priv->connection);
		}
		break;
		case PROP_OBJECT_PATH:
		{
			g_value_set_string(value, priv->object_path);
		}
		break;
		case PROP_CHANNEL_TYPE:
		{
			g_value_set_string(value, TP_IFACE_CHANNEL_TYPE_TEXT);
		}
		break;
		case PROP_HANDLE_TYPE:
		{
			g_value_set_uint(value, TP_HANDLE_TYPE_CONTACT);
		}
		break;
		case PROP_HANDLE:
		{
			g_value_set_uint(value, priv->handle);
		}
		break;
		default:
		{
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		}
		break;
	}
}

static void idle_im_channel_set_property(GObject *object, guint property_id, const GValue *value,
											GParamSpec *pspec)
{
	IdleIMChannel *chan = IDLE_IM_CHANNEL(object);
	IdleIMChannelPrivate *priv;

	g_assert(chan != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(chan));
		
	priv = IDLE_IM_CHANNEL_GET_PRIVATE(chan);

	switch (property_id)
	{
		case PROP_CONNECTION:
		{
			priv->connection = g_value_get_object(value);
		}
		break;
		case PROP_OBJECT_PATH:
		{
			if (priv->object_path)
			{
				g_free(priv->object_path);
			}

			priv->object_path = g_value_dup_string(value);
		}
		break;
		case PROP_HANDLE:
		{
			priv->handle = g_value_get_uint(value);
		}
		break;
		default:
		{
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		}
		break;
	}
}

static void
idle_im_channel_class_init (IdleIMChannelClass *idle_im_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (idle_im_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (idle_im_channel_class, sizeof (IdleIMChannelPrivate));

  object_class->constructor = idle_im_channel_constructor;

  object_class->get_property = idle_im_channel_get_property;
  object_class->set_property = idle_im_channel_set_property;

  object_class->dispose = idle_im_channel_dispose;
  object_class->finalize = idle_im_channel_finalize;

  param_spec = g_param_spec_object ("connection", "IdleConnection object",
                                    "The IdleConnection object that owns this "
                                    "IMChannel object.",
                                    IDLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                    "The D-Bus object path used for this "
                                    "object on the bus.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_string ("channel-type", "Telepathy channel type",
                                    "The D-Bus interface representing the "
                                    "type of this channel.",
                                    NULL,
                                    G_PARAM_READABLE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CHANNEL_TYPE, param_spec);

  param_spec = g_param_spec_uint ("handle-type", "Contact handle type",
                                  "The TpHandleType representing a "
                                  "contact handle.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_READABLE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE_TYPE, param_spec);

  param_spec = g_param_spec_uint ("handle", "Contact handle",
                                  "The TpHandle representing the contact "
                                  "with whom this channel communicates.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE, param_spec);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (idle_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_im_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (idle_im_channel_class), &dbus_glib_idle_im_channel_object_info);
}

void
idle_im_channel_dispose (GObject *object)
{
  IdleIMChannel *self = IDLE_IM_CHANNEL (object);
  IdleIMChannelPrivate *priv = IDLE_IM_CHANNEL_GET_PRIVATE (self);

  g_assert(object != NULL);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (!priv->closed)
  {
  	  g_signal_emit(self, signals[CLOSED], 0);
	  priv->closed = TRUE;
  }

  if (G_OBJECT_CLASS (idle_im_channel_parent_class)->dispose)
    G_OBJECT_CLASS (idle_im_channel_parent_class)->dispose (object);
}

void
idle_im_channel_finalize (GObject *object)
{
  IdleIMChannel *self = IDLE_IM_CHANNEL (object);
  IdleIMChannelPrivate *priv = IDLE_IM_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *handles;

  handles = priv->connection->handles[TP_HANDLE_TYPE_CONTACT];
  tp_handle_unref(handles, priv->handle);

  if (priv->object_path)
  {
  	g_free(priv->object_path);
  }

  G_OBJECT_CLASS (idle_im_channel_parent_class)->finalize (object);
}

gboolean _idle_im_channel_receive(IdleIMChannel *chan, TpChannelTextMessageType type, TpHandle sender, const gchar *text)
{
  time_t stamp = time(NULL);

	g_assert(chan != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(chan));

  return tp_text_mixin_receive(G_OBJECT(chan), type, sender, stamp, text);
}

void _idle_im_channel_rename(IdleIMChannel *chan, TpHandle new)
{
	IdleIMChannelPrivate *priv;
	TpHandleRepoIface *handles;
	gboolean valid;

	g_assert(chan != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(chan));

	g_assert(new != 0);

	priv = IDLE_IM_CHANNEL_GET_PRIVATE(chan);
	handles = priv->connection->handles[TP_HANDLE_TYPE_CONTACT];

	tp_handle_unref(handles, priv->handle);
	priv->handle = new;
	valid = tp_handle_ref(handles, priv->handle);
	g_assert(valid);

	g_debug("%s: changed to handle %u", G_STRFUNC, new);
}

/**
 * idle_im_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_im_channel_close (IdleIMChannel *obj, GError **error)
{
	IdleIMChannelPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(obj));

	priv = IDLE_IM_CHANNEL_GET_PRIVATE(obj);
	priv->closed = TRUE;

	g_debug("%s called on %p", G_STRFUNC, obj);
	g_signal_emit(obj, signals[CLOSED], 0);
	
	return TRUE;
}


/**
 * idle_im_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_im_channel_get_channel_type (IdleIMChannel *obj, gchar ** ret, GError **error)
{
	*ret = g_strdup(TP_IFACE_CHANNEL_TYPE_TEXT);

	return TRUE;
}


/**
 * idle_im_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_im_channel_get_handle (IdleIMChannel *obj, guint* ret, guint* ret1, GError **error)
{
	IdleIMChannelPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(obj));

	priv = IDLE_IM_CHANNEL_GET_PRIVATE(obj);

	*ret = TP_HANDLE_TYPE_CONTACT;
	*ret1 = priv->handle;
	
	return TRUE;
}


/**
 * idle_im_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_im_channel_get_interfaces (IdleIMChannel *obj, gchar *** ret, GError **error)
{
	const gchar *interfaces[] = {NULL};

	*ret = g_strdupv((gchar **)(interfaces));

	return TRUE;
}

/**
 * idle_im_channel_send
 *
 * Implements DBus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void idle_im_channel_send (TpSvcChannelTypeText *iface, guint type, const gchar * text, DBusGMethodInvocation *context)
{
	IdleIMChannel *obj = (IdleIMChannel *)(iface);
	IdleIMChannelPrivate *priv;
	gchar msg[IRC_MSG_MAXLEN+1];
	const char *recipient;
	time_t timestamp;
	const gchar *final_text = text;
	gsize len;
	gchar *part;
	gsize headerlen;
	GError *error;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(obj));

	priv = IDLE_IM_CHANNEL_GET_PRIVATE(obj);
	
	recipient = idle_handle_inspect(priv->connection->handles[TP_HANDLE_TYPE_CONTACT], priv->handle);
	
	if ((recipient == NULL) || (strlen(recipient) == 0))
	{
		g_debug("%s: invalid recipient", G_STRFUNC);

		error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "invalid recipient");
		dbus_g_method_return_error(context, error);
		g_error_free(error);

		return;
	}

	len = strlen(final_text);
	part = (gchar*)final_text;
	
	if (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL)
	{
		g_snprintf(msg, IRC_MSG_MAXLEN+1, "PRIVMSG %s :", recipient);
	}
	else if (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION)
	{
		g_snprintf(msg, IRC_MSG_MAXLEN+1, "PRIVMSG %s :\001ACTION ", recipient);
	}
	else if (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE)
	{
		g_snprintf(msg, IRC_MSG_MAXLEN+1, "NOTICE %s :", recipient);
	}
	else
	{
		g_debug("%s: invalid message type %u", G_STRFUNC, type);

		error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "invalid message type %u", type);
		dbus_g_method_return_error(context, error);
		g_error_free(error);

		return;
	}

	headerlen = strlen(msg);

	while (part < final_text+len)
	{
		char *br = strchr (part, '\n');
		size_t len = IRC_MSG_MAXLEN-headerlen;
		if (br)
		{
			len = (len < br - part) ? len : br - part;
		}

		if (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION)
		{
			g_snprintf(msg+headerlen, len + 1, "%s\001", part);
			len -= 1;
		}
		else
		{
			g_strlcpy(msg+headerlen, part, len + 1);
		}
		part += len;
		if (br)
		{
			part++;
		}

		_idle_connection_send(priv->connection, msg);
	}

	timestamp = time(NULL);
  tp_svc_channel_type_text_emit_sent((TpSvcChannelTypeText *)(obj), timestamp, type, text);
}

static void
text_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelTypeTextClass *klass = (TpSvcChannelTypeTextClass *)(g_iface);

  tp_text_mixin_iface_init(g_iface, iface_data);
#define IMPLEMENT(x) tp_svc_channel_type_text_implement_##x (\
    klass, idle_im_channel_##x)
  IMPLEMENT(send);
#undef IMPLEMENT
}

