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

#define _GNU_SOURCE
#include <string.h>
#include <time.h>

#include "idle-connection.h"
#include "idle-handles.h"
#include "telepathy-errors.h"
#include "telepathy-helpers.h"

#include "idle-im-channel.h"
#include "idle-im-channel-glue.h"
#include "idle-im-channel-signals-marshal.h"

#define IRC_MSG_MAXLEN 510

G_DEFINE_TYPE(IdleIMChannel, idle_im_channel, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CLOSED,
    RECEIVED,
    SENT,
	SEND_ERROR,
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

typedef struct _IdleIMPendingMessage IdleIMPendingMessage;

struct _IdleIMPendingMessage
{
	guint id;

	time_t timestamp;
	IdleHandle sender;
	
	TpChannelTextMessageType type;
	
	gchar *text;
};

/* private structure */
typedef struct _IdleIMChannelPrivate IdleIMChannelPrivate;

struct _IdleIMChannelPrivate
{
	IdleConnection *connection;
	gchar *object_path;
	IdleHandle handle;

	guint recv_id;
	GQueue *pending_messages;

	IdleIMPendingMessage *last_msg;
	
	gboolean closed;

  	gboolean dispose_has_run;
};

#define _idle_im_pending_new() \
	(g_slice_new(IdleIMPendingMessage))
#define _idle_im_pending_new0() \
	(g_slice_new0(IdleIMPendingMessage))

static void _idle_im_pending_free(IdleIMPendingMessage *msg)
{
	if (msg->text)
	{
		g_free(msg->text);
	}

	g_slice_free(IdleIMPendingMessage, msg);
}

#define IDLE_IM_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), IDLE_TYPE_IM_CHANNEL, IdleIMChannelPrivate))

static void
idle_im_channel_init (IdleIMChannel *obj)
{
	IdleIMChannelPrivate *priv = IDLE_IM_CHANNEL_GET_PRIVATE (obj);

	priv->pending_messages = g_queue_new();

	priv->last_msg = _idle_im_pending_new0();
}

static void idle_im_channel_dispose (GObject *object);
static void idle_im_channel_finalize (GObject *object);

static GObject *idle_im_channel_constructor(GType type, guint n_props, GObjectConstructParam *props)
{
	GObject *obj;
	IdleIMChannelPrivate *priv;
	DBusGConnection *bus;
	IdleHandleStorage *handles;
	gboolean valid;

	obj = G_OBJECT_CLASS(idle_im_channel_parent_class)->constructor(type, n_props, props);
	priv = IDLE_IM_CHANNEL_GET_PRIVATE(IDLE_IM_CHANNEL(obj));

	handles = _idle_connection_get_handles(priv->connection);
	valid = idle_handle_ref(handles, TP_HANDLE_TYPE_CONTACT, priv->handle);
	g_assert(valid);

	bus = tp_get_bus();
	dbus_g_connection_register_g_object(bus, priv->object_path, obj);

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
                                  "The IdleHandle representing the contact "
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

  signals[RECEIVED] =
    g_signal_new ("received",
                  G_OBJECT_CLASS_TYPE (idle_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_im_channel_marshal_VOID__INT_INT_INT_INT_INT_STRING,
                  G_TYPE_NONE, 6, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SENT] =
    g_signal_new ("sent",
                  G_OBJECT_CLASS_TYPE (idle_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  idle_im_channel_marshal_VOID__INT_INT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SEND_ERROR] =
	  g_signal_new("send-error",
			  		G_OBJECT_CLASS_TYPE(idle_im_channel_class),
					G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
					0,
					NULL, NULL,
					idle_im_channel_marshal_VOID__INT_INT_INT_STRING,
					G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

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
  IdleHandleStorage *handles;
  IdleIMPendingMessage *msg;

  handles = _idle_connection_get_handles(priv->connection);
  idle_handle_unref(handles, TP_HANDLE_TYPE_CONTACT, priv->handle);

  if (priv->object_path)
  {
  	g_free(priv->object_path);
  }

  while ((msg = g_queue_pop_head(priv->pending_messages)) != NULL)
  {
  	  _idle_im_pending_free(msg);
  }
	
  g_queue_free(priv->pending_messages);

  _idle_im_pending_free(priv->last_msg);

  G_OBJECT_CLASS (idle_im_channel_parent_class)->finalize (object);
}

gboolean _idle_im_channel_receive(IdleIMChannel *chan, TpChannelTextMessageType type, IdleHandle sender, const gchar *text)
{
	IdleIMChannelPrivate *priv;
	IdleIMPendingMessage *msg;

	g_assert(chan != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(chan));

	priv = IDLE_IM_CHANNEL_GET_PRIVATE(chan);

	msg = _idle_im_pending_new();

	msg->id = priv->recv_id++;
	msg->timestamp = time(NULL);
	msg->sender = sender;
	msg->type = type;
	msg->text = g_strdup(text);

	g_queue_push_tail(priv->pending_messages, msg);
	
	g_signal_emit(chan, signals[RECEIVED], 0,
					msg->id,
					msg->timestamp,
					msg->sender,
					msg->type,
					0,
					msg->text);

	g_debug("%s: queued message %u", G_STRFUNC, msg->id);

	return FALSE;
}

void _idle_im_channel_rename(IdleIMChannel *chan, IdleHandle new)
{
	IdleIMChannelPrivate *priv;
	IdleHandleStorage *handles;
	gboolean valid;

	g_assert(chan != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(chan));

	g_assert(new != 0);

	priv = IDLE_IM_CHANNEL_GET_PRIVATE(chan);
	handles = _idle_connection_get_handles(priv->connection);

	idle_handle_unref(handles, TP_HANDLE_TYPE_CONTACT, priv->handle);
	priv->handle = new;
	valid = idle_handle_ref(handles, TP_HANDLE_TYPE_CONTACT, priv->handle);
	g_assert(valid);

	g_debug("%s: changed to handle %u", G_STRFUNC, new);
}

void _idle_im_channel_nosuchnick(IdleIMChannel *chan)
{
	IdleIMChannelPrivate *priv;
	IdleIMPendingMessage *last_msg;
	
	g_assert(chan != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(chan));

	priv = IDLE_IM_CHANNEL_GET_PRIVATE(chan);

	g_assert(priv->last_msg != NULL);
	last_msg = priv->last_msg;
	
	/* TODO this is so incorrect, we are assuming it was always the most recent message etc... */

	g_signal_emit(chan, signals[SEND_ERROR], 0, TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE, last_msg->timestamp, last_msg->type, last_msg->text);
}

static gint idle_pending_message_compare(gconstpointer msg, gconstpointer id)
{
	IdleIMPendingMessage *message = (IdleIMPendingMessage *)(msg);

	return (message->id != GPOINTER_TO_INT(id));
}

/**
 * idle_im_channel_acknowledge_pending_messages
 *
 * Implements DBus method AcknowledgePendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_im_channel_acknowledge_pending_messages (IdleIMChannel *obj, const GArray *ids, GError **error)
{
	IdleIMChannelPrivate *priv;
	int i;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(obj));

	priv = IDLE_IM_CHANNEL_GET_PRIVATE(obj);

	for (i=0; i < ids->len; i++)
	{
		GList *node;
		IdleIMPendingMessage *msg;
		guint id = g_array_index(ids, guint, i);

		node = g_queue_find_custom(priv->pending_messages,
								   GINT_TO_POINTER(id),
								   idle_pending_message_compare);

		if (!node)
		{
			g_debug("%s: message id %u not found", G_STRFUNC, id);

			*error = g_error_new(TELEPATHY_ERRORS, InvalidArgument, "message id %u not found", id);

			return FALSE;
		}

		msg = (IdleIMPendingMessage *)(node->data);

		g_debug("%s: acknowledging message id %u", G_STRFUNC, id);

		g_queue_delete_link(priv->pending_messages, node);

		_idle_im_pending_free(msg);
	}
	
	return TRUE;
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
 * idle_im_channel_list_pending_messages
 *
 * Implements DBus method ListPendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean idle_im_channel_list_pending_messages (IdleIMChannel *obj,
                                                gboolean clear,
                                                GPtrArray ** ret,
                                                GError **error)
{
	IdleIMChannelPrivate *priv;
	guint count;
	GPtrArray *messages;
	GList *cur;

	g_assert (IDLE_IS_IM_CHANNEL (obj));

	priv = IDLE_IM_CHANNEL_GET_PRIVATE (obj);

	count = g_queue_get_length (priv->pending_messages);
	messages = g_ptr_array_sized_new (count);

	for (cur = g_queue_peek_head_link(priv->pending_messages);
		 cur != NULL;
		 cur = cur->next)
	{
		IdleIMPendingMessage *msg = (IdleIMPendingMessage *)(cur->data);
		GValueArray *vals;

		vals = g_value_array_new (6);

		g_value_array_append (vals, NULL);
		g_value_init (g_value_array_get_nth (vals, 0), G_TYPE_UINT);
		g_value_set_uint (g_value_array_get_nth (vals, 0), msg->id);

		g_value_array_append (vals, NULL);
		g_value_init (g_value_array_get_nth (vals, 1), G_TYPE_UINT);
		g_value_set_uint (g_value_array_get_nth (vals, 1), msg->timestamp);

		g_value_array_append (vals, NULL);
		g_value_init (g_value_array_get_nth (vals, 2), G_TYPE_UINT);
		g_value_set_uint (g_value_array_get_nth (vals, 2), msg->sender);

		g_value_array_append (vals, NULL);
		g_value_init (g_value_array_get_nth (vals, 3), G_TYPE_UINT);
		g_value_set_uint (g_value_array_get_nth (vals, 3), msg->type);

		g_value_array_append (vals, NULL);
		g_value_init (g_value_array_get_nth (vals, 4), G_TYPE_UINT);
		g_value_set_uint (g_value_array_get_nth (vals, 4), 0);

		g_value_array_append (vals, NULL);
		g_value_init (g_value_array_get_nth (vals, 5), G_TYPE_STRING);
		g_value_set_string (g_value_array_get_nth (vals, 5), msg->text);

		g_ptr_array_add (messages, vals);
	}

	if (clear)
	{
		IdleIMPendingMessage *msg;

		while ((msg = g_queue_pop_head(priv->pending_messages)) != NULL)
		{
			_idle_im_pending_free(msg);
		}
	}

	*ret = messages;

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
gboolean idle_im_channel_send (IdleIMChannel *obj, guint type, const gchar * text, GError **error)
{
	IdleIMChannelPrivate *priv;
	gchar msg[IRC_MSG_MAXLEN+1];
	const char *recipient;
	time_t timestamp;
	const gchar *final_text = text;
	gsize len;
	gchar *part;
	gsize headerlen;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(obj));

	priv = IDLE_IM_CHANNEL_GET_PRIVATE(obj);
	
	recipient = idle_handle_inspect(_idle_connection_get_handles(priv->connection), TP_HANDLE_TYPE_CONTACT,
										priv->handle);
	
	if ((recipient == NULL) || (strlen(recipient) == 0))
	{
		g_debug("%s: invalid recipient", G_STRFUNC);

		*error = g_error_new(TELEPATHY_ERRORS, NotAvailable, "invalid recipient");

		return FALSE;
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

		*error = g_error_new(TELEPATHY_ERRORS, InvalidArgument, "invalid message type %u", type);

		return FALSE;
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

	g_signal_emit(obj, signals[SENT], 0, timestamp, type, text);
	
	if (priv->last_msg->text)
	{
		g_free(priv->last_msg->text);
	}

	priv->last_msg->timestamp = timestamp;
	priv->last_msg->type = type;
	priv->last_msg->text = g_strdup(text);
	
	return TRUE;
}

