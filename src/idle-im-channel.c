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

#include <glib.h>
#include <dbus/dbus-glib.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/svc-channel.h>

#include <time.h>

#include "idle-connection.h"
#include "idle-handles.h"
#include "idle-text.h"

#include "idle-im-channel.h"

#define IDLE_DEBUG_FLAG IDLE_DEBUG_IM
#include "idle-debug.h"

static void channel_iface_init (gpointer, gpointer);
static void text_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(IdleIMChannel, idle_im_channel, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL_TYPE_TEXT, text_iface_init);
		G_IMPLEMENT_INTERFACE(TP_TYPE_CHANNEL_IFACE, NULL);)

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

	obj = G_OBJECT_CLASS(idle_im_channel_parent_class)->constructor(type, n_props, props);
	priv = IDLE_IM_CHANNEL_GET_PRIVATE(IDLE_IM_CHANNEL(obj));

	handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT);
	tp_handle_ref(handles, priv->handle);
	g_assert(tp_handle_is_valid(tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT), priv->handle, NULL));

	bus = tp_get_bus();
	dbus_g_connection_register_g_object(bus, priv->object_path, obj);

  tp_text_mixin_init(obj, G_STRUCT_OFFSET(IdleIMChannel, text), handles);
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

static void idle_im_channel_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
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
		case PROP_HANDLE_TYPE:
		{
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

  g_object_class_override_property (object_class, PROP_OBJECT_PATH, "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE, "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE, "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  param_spec = g_param_spec_object ("connection", "IdleConnection object",
                                    "The IdleConnection object that owns this "
                                    "IMChannel object.",
                                    IDLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

	tp_text_mixin_class_init(object_class, G_STRUCT_OFFSET(IdleIMChannelClass, text_class));
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
		tp_svc_channel_emit_closed((TpSvcChannel *)(self));

  if (G_OBJECT_CLASS (idle_im_channel_parent_class)->dispose)
    G_OBJECT_CLASS (idle_im_channel_parent_class)->dispose (object);
}

void
idle_im_channel_finalize (GObject *object)
{
  IdleIMChannel *self = IDLE_IM_CHANNEL (object);
  IdleIMChannelPrivate *priv = IDLE_IM_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *handles;

  handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT);
  tp_handle_unref(handles, priv->handle);

  if (priv->object_path)
  {
  	g_free(priv->object_path);
  }

	tp_text_mixin_finalize(object);

  G_OBJECT_CLASS (idle_im_channel_parent_class)->finalize (object);
}

gboolean _idle_im_channel_receive(IdleIMChannel *chan, TpChannelTextMessageType type, TpHandle sender, const gchar *text)
{
  time_t stamp = time(NULL);

	g_assert(chan != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(chan));

  return tp_text_mixin_receive(G_OBJECT(chan), type, sender, stamp, text);
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
static void idle_im_channel_close (TpSvcChannel *iface, DBusGMethodInvocation *context)
{
	IdleIMChannel *obj = IDLE_IM_CHANNEL(iface);
	IdleIMChannelPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(obj));

	priv = IDLE_IM_CHANNEL_GET_PRIVATE(obj);
	priv->closed = TRUE;

	IDLE_DEBUG("called on %p", obj);

	tp_svc_channel_emit_closed(iface);

	tp_svc_channel_return_from_close(context);
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
static void idle_im_channel_get_channel_type (TpSvcChannel *iface, DBusGMethodInvocation *context)
{
	tp_svc_channel_return_from_get_channel_type(context, TP_IFACE_CHANNEL_TYPE_TEXT);
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
static void idle_im_channel_get_handle (TpSvcChannel *iface, DBusGMethodInvocation *context)
{
	IdleIMChannel *obj = IDLE_IM_CHANNEL(iface);
	IdleIMChannelPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(obj));

	priv = IDLE_IM_CHANNEL_GET_PRIVATE(obj);

	tp_svc_channel_return_from_get_handle(context, TP_HANDLE_TYPE_CONTACT, priv->handle);
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
static void idle_im_channel_get_interfaces (TpSvcChannel *iface, DBusGMethodInvocation *context)
{
	const gchar *interfaces[] = {NULL};

	tp_svc_channel_return_from_get_interfaces(context, interfaces);
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
	IdleIMChannelPrivate *priv = IDLE_IM_CHANNEL_GET_PRIVATE(obj);
	const gchar *recipient = tp_handle_inspect(tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT), priv->handle);
	GError *error;

	if ((recipient == NULL) || (recipient[0] == '\0'))
	{
		IDLE_DEBUG("invalid recipient");

		error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "invalid recipient");
		dbus_g_method_return_error(context, error);
		g_error_free(error);

		return;
	}

	idle_text_send((GObject *)(obj), type, recipient, text, priv->connection, context);
}

static void
channel_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, idle_im_channel_##x)
  IMPLEMENT(close);
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
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

