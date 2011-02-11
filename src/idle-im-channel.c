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

#include "idle-im-channel.h"

#include <time.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-generic.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_IM
#include "idle-connection.h"
#include "idle-debug.h"
#include "idle-handles.h"
#include "idle-text.h"

static void _channel_iface_init(gpointer, gpointer);
static void _destroyable_iface_init(gpointer, gpointer);

static void idle_im_channel_send (GObject *obj, TpMessage *message, TpMessageSendingFlags flags);

G_DEFINE_TYPE_WITH_CODE(IdleIMChannel, idle_im_channel, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL, _channel_iface_init);
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL_TYPE_TEXT, tp_message_mixin_text_iface_init);
		G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MESSAGES, tp_message_mixin_messages_iface_init);
		G_IMPLEMENT_INTERFACE(TP_TYPE_CHANNEL_IFACE, NULL);
		G_IMPLEMENT_INTERFACE(TP_TYPE_EXPORTABLE_CHANNEL, NULL);
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL_INTERFACE_DESTROYABLE, _destroyable_iface_init)
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_DBUS_PROPERTIES, tp_dbus_properties_mixin_iface_init);)

/* property enum */
enum {
	PROP_CONNECTION = 1,
	PROP_OBJECT_PATH,
	PROP_INTERFACES,
	PROP_CHANNEL_TYPE,
	PROP_HANDLE_TYPE,
	PROP_HANDLE,
	PROP_TARGET_ID,
	PROP_REQUESTED,
	PROP_INITIATOR_HANDLE,
	PROP_INITIATOR_ID,
	PROP_CHANNEL_DESTROYED,
	PROP_CHANNEL_PROPERTIES,
	LAST_PROPERTY_ENUM
};

const gchar *im_channel_interfaces[] = {
	TP_IFACE_CHANNEL_INTERFACE_MESSAGES,
	TP_IFACE_CHANNEL_INTERFACE_DESTROYABLE,
	NULL
};

/* private structure */
typedef struct _IdleIMChannelPrivate IdleIMChannelPrivate;

struct _IdleIMChannelPrivate {
	IdleConnection *connection;
	gchar *object_path;
	TpHandle handle;
	TpHandle initiator;

	gboolean closed;

	gboolean dispose_has_run;
};

#define IDLE_IM_CHANNEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), IDLE_TYPE_IM_CHANNEL, IdleIMChannelPrivate))

static void idle_im_channel_init (IdleIMChannel *obj) {
}

static void idle_im_channel_dispose (GObject *object);
static void idle_im_channel_finalize (GObject *object);

static GObject *idle_im_channel_constructor(GType type, guint n_props, GObjectConstructParam *props) {
	GObject *obj;
	IdleIMChannelPrivate *priv;
	TpDBusDaemon *bus;
	TpHandleRepoIface *handles;
	TpBaseConnection *conn;
	TpChannelTextMessageType types[] = {
			TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
			TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
			TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
	};
	const gchar * supported_content_types[] = {
			"text/plain",
			NULL
	};

	obj = G_OBJECT_CLASS(idle_im_channel_parent_class)->constructor(type, n_props, props);
	priv = IDLE_IM_CHANNEL_GET_PRIVATE(IDLE_IM_CHANNEL(obj));

	conn = TP_BASE_CONNECTION(priv->connection);

	handles = tp_base_connection_get_handles(conn, TP_HANDLE_TYPE_CONTACT);
	tp_handle_ref(handles, priv->handle);
	tp_handle_ref(handles, priv->initiator);
	g_assert(tp_handle_is_valid(tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT), priv->handle, NULL));

	bus = tp_base_connection_get_dbus_daemon (conn);
	tp_dbus_daemon_register_object (bus, priv->object_path, obj);

	/* initialize message mixin */
	tp_message_mixin_init (obj, G_STRUCT_OFFSET (IdleIMChannel, message_mixin),
			conn);
	tp_message_mixin_implement_sending (obj, idle_im_channel_send,
			G_N_ELEMENTS (types), types, 0,
			TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_FAILURES,
			supported_content_types);

	return obj;
}

static void idle_im_channel_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
	IdleIMChannel *chan;
	IdleIMChannelPrivate *priv;
	TpHandleRepoIface *handles;

	g_assert(object != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(object));

	chan = IDLE_IM_CHANNEL(object);
	priv = IDLE_IM_CHANNEL_GET_PRIVATE(chan);
	handles = tp_base_connection_get_handles(
		TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT);

	switch (property_id) {
		case PROP_CONNECTION:
			g_value_set_object(value, priv->connection);
			break;

		case PROP_OBJECT_PATH:
			g_value_set_string(value, priv->object_path);
			break;

		case PROP_INTERFACES:
			g_value_set_static_boxed(value, im_channel_interfaces);
			break;

		case PROP_CHANNEL_TYPE:
			g_value_set_string(value, TP_IFACE_CHANNEL_TYPE_TEXT);
			break;

		case PROP_HANDLE_TYPE:
			g_value_set_uint(value, TP_HANDLE_TYPE_CONTACT);
			break;

		case PROP_HANDLE:
			g_value_set_uint(value, priv->handle);
			break;

		case PROP_TARGET_ID:
			g_value_set_string(value,
				tp_handle_inspect(handles, priv->handle));
			break;

		case PROP_REQUESTED:
			g_value_set_boolean(value, priv->initiator != priv->handle);
			break;

		case PROP_INITIATOR_HANDLE:
			g_value_set_uint(value, priv->initiator);
			break;

		case PROP_INITIATOR_ID:
			g_value_set_string(value,
				tp_handle_inspect(handles, priv->initiator));
			break;

		case PROP_CHANNEL_DESTROYED:
			g_value_set_boolean (value, priv->closed);
			break;

		case PROP_CHANNEL_PROPERTIES:
		{
			GHashTable *props =
				tp_dbus_properties_mixin_make_properties_hash (
					object,
					TP_IFACE_CHANNEL, "Interfaces",
					TP_IFACE_CHANNEL, "ChannelType",
					TP_IFACE_CHANNEL, "TargetHandleType",
					TP_IFACE_CHANNEL, "TargetHandle",
					TP_IFACE_CHANNEL, "TargetID",
					TP_IFACE_CHANNEL, "InitiatorHandle",
					TP_IFACE_CHANNEL, "InitiatorID",
					TP_IFACE_CHANNEL, "Requested",
					NULL);
			g_value_take_boxed (value, props);
			break;
		}

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void idle_im_channel_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
	IdleIMChannel *chan = IDLE_IM_CHANNEL(object);
	IdleIMChannelPrivate *priv;

	g_assert(chan != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(chan));

	priv = IDLE_IM_CHANNEL_GET_PRIVATE(chan);

	switch (property_id) {
		case PROP_CONNECTION:
			priv->connection = g_value_get_object(value);
			break;

		case PROP_OBJECT_PATH:
			if (priv->object_path)
				g_free(priv->object_path);

			priv->object_path = g_value_dup_string(value);
			break;

		case PROP_HANDLE:
			priv->handle = g_value_get_uint(value);
			break;

		case PROP_INITIATOR_HANDLE:
			priv->initiator = g_value_get_uint(value);
			break;

		case PROP_CHANNEL_TYPE:
		case PROP_HANDLE_TYPE:
			/* writeable in the interface, but setting them makes
			no sense, so ignore them */
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void idle_im_channel_class_init (IdleIMChannelClass *idle_im_channel_class) {
	GObjectClass *object_class = G_OBJECT_CLASS(idle_im_channel_class);
	GParamSpec *param_spec;

	g_type_class_add_private (idle_im_channel_class, sizeof (IdleIMChannelPrivate));

	object_class->constructor = idle_im_channel_constructor;

	object_class->get_property = idle_im_channel_get_property;
	object_class->set_property = idle_im_channel_set_property;

	object_class->dispose = idle_im_channel_dispose;
	object_class->finalize = idle_im_channel_finalize;

	g_object_class_override_property(object_class, PROP_OBJECT_PATH, "object-path");
	g_object_class_override_property(object_class, PROP_CHANNEL_TYPE, "channel-type");
	g_object_class_override_property(object_class, PROP_HANDLE_TYPE, "handle-type");
	g_object_class_override_property(object_class, PROP_HANDLE, "handle");
	g_object_class_override_property(object_class, PROP_CHANNEL_DESTROYED, "channel-destroyed");
	g_object_class_override_property(object_class, PROP_CHANNEL_PROPERTIES, "channel-properties");

	param_spec = g_param_spec_object("connection", "IdleConnection object",
									 "The IdleConnection object that owns this "
									 "IMChannel object.",
									 IDLE_TYPE_CONNECTION,
									 G_PARAM_CONSTRUCT_ONLY |
									 G_PARAM_READWRITE |
									 G_PARAM_STATIC_NICK |
									 G_PARAM_STATIC_BLURB);
	g_object_class_install_property(object_class, PROP_CONNECTION, param_spec);

	param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
		"Interfaces implemented by this object besides Channel",
		G_TYPE_STRV, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

	param_spec = g_param_spec_string ("target-id", "Contact's identifier",
		"The name of the person we're speaking to",
		NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

	param_spec = g_param_spec_boolean ("requested", "Requested?",
		"True if this channel was requested by the local user",
		FALSE,
		G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

	param_spec = g_param_spec_uint ("initiator-handle", "Initiator's handle",
		"The contact who initiated the channel",
		0, G_MAXUINT32, 0,
		G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_INITIATOR_HANDLE, param_spec);

	param_spec = g_param_spec_string ("initiator-id", "Initiator's bare JID",
		"The string obtained by inspecting the initiator-handle",
		NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_INITIATOR_ID, param_spec);

	static TpDBusPropertiesMixinPropImpl channel_props[] = {
		{ "Interfaces", "interfaces", NULL },
		{ "ChannelType", "channel-type", NULL },
		{ "TargetHandleType", "handle-type", NULL },
		{ "TargetHandle", "handle", NULL },
		{ "TargetID", "target-id", NULL },
		{ "InitiatorHandle", "initiator-handle", NULL },
		{ "InitiatorID", "initiator-id", NULL },
		{ "Requested", "requested", NULL },
		{ NULL }
	};
	static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
		{ TP_IFACE_CHANNEL,
			tp_dbus_properties_mixin_getter_gobject_properties,
			NULL,
			channel_props,
		},
		{ NULL }
	};

	idle_im_channel_class->dbus_props_class.interfaces = prop_interfaces;
	tp_dbus_properties_mixin_class_init(object_class, G_STRUCT_OFFSET(IdleIMChannelClass, dbus_props_class));
	tp_message_mixin_init_dbus_properties (object_class);
}

void idle_im_channel_dispose (GObject *object) {
	IdleIMChannel *self = IDLE_IM_CHANNEL(object);
	IdleIMChannelPrivate *priv = IDLE_IM_CHANNEL_GET_PRIVATE(self);

	g_assert(object != NULL);

	if (priv->dispose_has_run)
		return;

	priv->dispose_has_run = TRUE;

	if (!priv->closed)
		tp_svc_channel_emit_closed((TpSvcChannel *)(self));

	if (G_OBJECT_CLASS(idle_im_channel_parent_class)->dispose)
		G_OBJECT_CLASS(idle_im_channel_parent_class)->dispose (object);
}

void idle_im_channel_finalize (GObject *object) {
	IdleIMChannel *self = IDLE_IM_CHANNEL(object);
	IdleIMChannelPrivate *priv = IDLE_IM_CHANNEL_GET_PRIVATE(self);
	TpHandleRepoIface *handles;

	handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT);
	tp_handle_unref(handles, priv->handle);
	tp_handle_unref(handles, priv->initiator);

	if (priv->object_path)
		g_free(priv->object_path);

	tp_message_mixin_finalize (object);

	G_OBJECT_CLASS(idle_im_channel_parent_class)->finalize (object);
}

gboolean idle_im_channel_receive(IdleIMChannel *chan, TpChannelTextMessageType type, TpHandle sender, const gchar *text) {
	IdleIMChannelPrivate *priv = IDLE_IM_CHANNEL_GET_PRIVATE (chan);
	TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->connection);

	return idle_text_received (G_OBJECT (chan), base_conn, type, text, sender);
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
static void idle_im_channel_close (TpSvcChannel *iface, DBusGMethodInvocation *context) {
	IdleIMChannel *obj = IDLE_IM_CHANNEL(iface);
	IdleIMChannelPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_IM_CHANNEL(obj));

	priv = IDLE_IM_CHANNEL_GET_PRIVATE(obj);

	IDLE_DEBUG("called on %p", obj);

	/* The IM manager will resurrect the channel if we have pending
	 * messages. When we're resurrected, we want the initiator
	 * to be the contact who sent us those messages, if it isn't already */
	if (tp_message_mixin_has_pending_messages ((GObject *)obj, NULL)) {
		IDLE_DEBUG("Not really closing, I still have pending messages");

		if (priv->initiator != priv->handle) {
			TpHandleRepoIface *contact_repo =
				tp_base_connection_get_handles(
					(TpBaseConnection *) priv->connection,
					TP_HANDLE_TYPE_CONTACT);

			g_assert(priv->initiator != 0);
			g_assert(priv->handle != 0);

			tp_handle_unref(contact_repo, priv->initiator);
			priv->initiator = priv->handle;
			tp_handle_ref(contact_repo, priv->initiator);
		}

		tp_message_mixin_set_rescued ((GObject *) obj);
	} else {
		IDLE_DEBUG ("Actually closing, I have no pending messages");
		priv->closed = TRUE;
	}

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
static void idle_im_channel_get_channel_type (TpSvcChannel *iface, DBusGMethodInvocation *context) {
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
static void idle_im_channel_get_handle (TpSvcChannel *iface, DBusGMethodInvocation *context) {
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
static void idle_im_channel_get_interfaces(TpSvcChannel *iface, DBusGMethodInvocation *context) {
	tp_svc_channel_return_from_get_interfaces(context, im_channel_interfaces);
}

/**
 * idle_im_channel_send
 *
 * Indirectly implements (via TpMessageMixin) D-Bus method Send on interface
 * org.freedesktop.Telepathy.Channel.Type.Text and D-Bus method SendMessage on
 * Channel.Interface.Messages
 */
static void idle_im_channel_send (GObject *obj, TpMessage *message, TpMessageSendingFlags flags) {
	IdleIMChannel *self = (IdleIMChannel *) obj;
	IdleIMChannelPrivate *priv = IDLE_IM_CHANNEL_GET_PRIVATE(self);
	const gchar *recipient = tp_handle_inspect(tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT), priv->handle);

	idle_text_send (obj, message, flags, recipient, priv->connection);
}

static void idle_im_channel_destroy(TpSvcChannelInterfaceDestroyable *iface, DBusGMethodInvocation *context) {
	IdleIMChannel *obj = (IdleIMChannel *)(iface);
	IdleIMChannelPrivate *priv = IDLE_IM_CHANNEL_GET_PRIVATE(obj);

	IDLE_DEBUG ("called on %p with %spending messages", obj,
		tp_message_mixin_has_pending_messages ((GObject *)obj, NULL) ? "" : "no ");

	priv->closed = TRUE;
	tp_svc_channel_emit_closed(iface);
	tp_svc_channel_interface_destroyable_return_from_destroy(context);
}

static void _channel_iface_init(gpointer g_iface, gpointer iface_data) {
	TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
		klass, idle_im_channel_##x)
	IMPLEMENT(close);
	IMPLEMENT(get_channel_type);
	IMPLEMENT(get_handle);
	IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}

static void _destroyable_iface_init(gpointer klass, gpointer iface_data) {
#define IMPLEMENT(x) tp_svc_channel_interface_destroyable_implement_##x (\
		klass, idle_im_channel_##x)
	IMPLEMENT (destroy);
#undef IMPLEMENT
}
