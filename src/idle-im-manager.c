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

#include "idle-im-manager.h"

#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_IM
#include "idle-connection.h"
#include "idle-ctcp.h"
#include "idle-debug.h"
#include "idle-im-channel.h"
#include "idle-parser.h"
#include "idle-text.h"

static void _im_manager_iface_init(gpointer g_iface, gpointer iface_data);
static void _im_manager_constructed (GObject *obj);
static void _im_manager_dispose (GObject *object);

G_DEFINE_TYPE_WITH_CODE(IdleIMManager, idle_im_manager, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(TP_TYPE_CHANNEL_MANAGER, _im_manager_iface_init));

/* properties */
enum {
	PROP_CONNECTION = 1,
	LAST_PROPERTY_ENUM
};

static const gchar * const im_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const im_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    NULL
};

typedef struct _IdleIMManagerPrivate IdleIMManagerPrivate;
struct _IdleIMManagerPrivate {
	IdleConnection *conn;
	GHashTable *channels;
	int status_changed_id;
	gboolean dispose_has_run;
};

#define IDLE_IM_MANAGER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IDLE_TYPE_IM_MANAGER, IdleIMManagerPrivate))

static IdleParserHandlerResult _notice_privmsg_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);

static void _im_manager_close_all(IdleIMManager *manager);
static void connection_status_changed_cb (IdleConnection* conn, guint status, guint reason, IdleIMManager *self);

static void _im_manager_foreach(TpChannelManager *manager, TpExportableChannelFunc func, gpointer user_data);
static void _im_manager_type_foreach_class (GType type, TpChannelManagerTypeChannelClassFunc func, gpointer user_data);

//static TpChannelManagerRequestStatus _iface_request(TpChannelFactoryIface *iface, const gchar *chan_type, TpHandleType handle_type, guint handle, gpointer request, TpChannelIface **new_chan, GError **error);

static gboolean _im_manager_create_channel(TpChannelManager *manager, gpointer request_token, GHashTable *request_properties);
static gboolean _im_manager_request_channel(TpChannelManager *manager, gpointer request_token, GHashTable *request_properties);
static gboolean _im_manager_ensure_channel(TpChannelManager *manager, gpointer request_token, GHashTable *request_properties);
static gboolean _im_manager_requestotron (IdleIMManager *self, gpointer request_token, GHashTable *request_properties, gboolean require_new);
static IdleIMChannel *_im_manager_new_channel (IdleIMManager *mgr, TpHandle handle, TpHandle initiator, gpointer request);

static void _im_channel_closed_cb (IdleIMChannel *chan, gpointer user_data);

static void idle_im_manager_init(IdleIMManager *obj) {
	IdleIMManagerPrivate *priv = IDLE_IM_MANAGER_GET_PRIVATE(obj);
	priv->channels = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_object_unref);
	priv->status_changed_id = 0;
	priv->dispose_has_run = FALSE;
}

static void idle_im_manager_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
	IdleIMManager *mgr = IDLE_IM_MANAGER(object);
	IdleIMManagerPrivate *priv = IDLE_IM_MANAGER_GET_PRIVATE(mgr);

	switch (property_id) {
		case PROP_CONNECTION:
			g_value_set_object(value, priv->conn);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void idle_im_manager_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
	IdleIMManager *manager = IDLE_IM_MANAGER(object);
	IdleIMManagerPrivate *priv = IDLE_IM_MANAGER_GET_PRIVATE(manager);

	switch (property_id) {
		case PROP_CONNECTION:
			priv->conn = g_value_get_object(value);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void idle_im_manager_class_init(IdleIMManagerClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *param_spec;

	g_type_class_add_private(klass, sizeof(IdleIMManagerPrivate));

	object_class->constructed = _im_manager_constructed;
	object_class->dispose = _im_manager_dispose;
	object_class->get_property = idle_im_manager_get_property;
	object_class->set_property = idle_im_manager_set_property;

	param_spec = g_param_spec_object("connection", "IdleConnection object", "The IdleConnection object that owns this IM channel manager object.", IDLE_TYPE_CONNECTION, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
	g_object_class_install_property(object_class, PROP_CONNECTION, param_spec);
}

static void
_im_manager_constructed (GObject *obj)
{
	IdleIMManager *self = IDLE_IM_MANAGER (obj);
	IdleIMManagerPrivate *priv = IDLE_IM_MANAGER_GET_PRIVATE (self);

	G_OBJECT_CLASS (idle_im_manager_parent_class)->constructed (obj);

	g_return_if_fail (priv->conn);

	priv->status_changed_id = g_signal_connect (priv->conn,
												"status-changed", (GCallback)
												connection_status_changed_cb,
												self);
}


static IdleParserHandlerResult _notice_privmsg_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleIMManager *manager = IDLE_IM_MANAGER(user_data);
	IdleIMManagerPrivate *priv = IDLE_IM_MANAGER_GET_PRIVATE(manager);
	TpHandle handle = (TpHandle) g_value_get_uint(g_value_array_get_nth(args, 0));
	IdleIMChannel *chan;
	TpChannelTextMessageType type;
	gchar *body;

	if (code == IDLE_PARSER_PREFIXCMD_NOTICE_USER) {
		type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE;
		body = idle_ctcp_kill_blingbling(g_value_get_string(g_value_array_get_nth(args, 2)));
	} else {
		gboolean decoded = idle_text_decode(g_value_get_string(g_value_array_get_nth(args, 2)), &type, &body);
		if (!decoded)
			return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	idle_connection_emit_queued_aliases_changed(priv->conn);

	if (!priv->channels) {
		IDLE_DEBUG("Channels hash table missing, ignoring...");
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	}

	if (!(chan = g_hash_table_lookup(priv->channels, GUINT_TO_POINTER(handle))))
		chan = _im_manager_new_channel(manager, handle, handle, NULL);

	idle_im_channel_receive(chan, type, handle, body);

	g_free(body);

	return IDLE_PARSER_HANDLER_RESULT_HANDLED;
}

static void _im_manager_close_all(IdleIMManager *manager) {
	IdleIMManagerPrivate *priv = IDLE_IM_MANAGER_GET_PRIVATE(manager);

	if (priv->channels) {
		GHashTable *tmp = priv->channels;
		priv->channels = NULL;
		g_hash_table_destroy(tmp);
	}
	if (priv->status_changed_id != 0) {
		g_signal_handler_disconnect (priv->conn, priv->status_changed_id);
		priv->status_changed_id = 0;
	}
}

static void connection_status_changed_cb (IdleConnection* conn,
										  guint status,
										  guint reason,
										  IdleIMManager *self)
{
	IdleIMManagerPrivate *priv = IDLE_IM_MANAGER_GET_PRIVATE(self);

	switch (status) {
		case TP_CONNECTION_STATUS_DISCONNECTED:
			idle_parser_remove_handlers_by_data(priv->conn->parser, self);
			_im_manager_close_all (self);
			break;

		case TP_CONNECTION_STATUS_CONNECTED:
			idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_NOTICE_USER, _notice_privmsg_handler, self);
			idle_parser_add_handler(priv->conn->parser, IDLE_PARSER_PREFIXCMD_PRIVMSG_USER, _notice_privmsg_handler, self);
			break;

		default:
			/* Nothing to do. */
			break;
	}
}

struct _ForeachHelperData {
	TpExportableChannelFunc func;
	gpointer user_data;
};

static void _foreach_helper(gpointer key, gpointer value, gpointer user_data) {
	struct _ForeachHelperData *data = user_data;
	data->func(value, data->user_data);
}

static void _im_manager_foreach(TpChannelManager *manager, TpExportableChannelFunc func, gpointer user_data) {
	IdleIMManagerPrivate *priv = IDLE_IM_MANAGER_GET_PRIVATE(manager);
	struct _ForeachHelperData data = {func, user_data};

	if (!priv->channels) {
		IDLE_DEBUG("Channels hash table missing, ignoring...");
		return;
	}

	g_hash_table_foreach(priv->channels, _foreach_helper, &data);
}


static void _im_manager_type_foreach_class (GType type,
									   TpChannelManagerTypeChannelClassFunc func,
									   gpointer user_data)
{
	GHashTable *table;
	GValue *value;

	table = g_hash_table_new_full (g_str_hash, g_str_equal,
								   NULL, (GDestroyNotify) tp_g_value_slice_free);

	value = tp_g_value_slice_new (G_TYPE_STRING);
	g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
	g_hash_table_insert (table, (gpointer) im_channel_fixed_properties[0], value);

	value = tp_g_value_slice_new (G_TYPE_UINT);
	g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
	g_hash_table_insert (table, (gpointer) im_channel_fixed_properties[1], value);

	func (type, table, im_channel_allowed_properties, user_data);

	g_hash_table_destroy (table);
}


static gboolean
_im_manager_create_channel(TpChannelManager *manager,
						   gpointer request_token,
						   GHashTable *request_properties)
{
	IdleIMManager *self = IDLE_IM_MANAGER (manager);

	return _im_manager_requestotron (self, request_token, request_properties,
									 TRUE);
}


static gboolean
_im_manager_request_channel(TpChannelManager *manager,
							gpointer request_token,
							GHashTable *request_properties)
{
	IdleIMManager *self = IDLE_IM_MANAGER (manager);

	return _im_manager_requestotron (self, request_token, request_properties,
									 FALSE);
}


static gboolean
_im_manager_ensure_channel(TpChannelManager *manager,
						   gpointer request_token,
						   GHashTable *request_properties)
{
	IdleIMManager *self = IDLE_IM_MANAGER (manager);

	return _im_manager_requestotron (self, request_token, request_properties,
									 FALSE);
}


static gboolean
_im_manager_requestotron (IdleIMManager *self,
						  gpointer request_token,
						  GHashTable *request_properties,
						  gboolean require_new)
{
	IdleIMManagerPrivate *priv = IDLE_IM_MANAGER_GET_PRIVATE (self);
	TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;
	TpHandleRepoIface *contact_repo =
		tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);
	TpHandle handle;
	GError *error = NULL;
	TpExportableChannel *channel;

	if (tp_strdiff (tp_asv_get_string (request_properties,
									   TP_IFACE_CHANNEL ".ChannelType"), TP_IFACE_CHANNEL_TYPE_TEXT))
		return FALSE;

	if (tp_asv_get_uint32 (request_properties,
						   TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_CONTACT)
		return FALSE;

	handle = tp_asv_get_uint32 (request_properties,
								TP_IFACE_CHANNEL ".TargetHandle", NULL);

	if (!tp_handle_is_valid (contact_repo, handle, &error))
		goto error;

	/* Check if there are any other properties that we don't understand */
	if (tp_channel_manager_asv_has_unknown_properties (request_properties,
													   im_channel_fixed_properties,
													   im_channel_allowed_properties,
													   &error))
	{
		goto error;
	}

	/* Don't support opening a channel to our self handle */
	if (handle == base_conn->self_handle)
	{
		g_set_error (&error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
					 "Can't open a text channel to yourself");
		goto error;
	}

	channel = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle));

	if (channel == NULL)
	{
		_im_manager_new_channel (self, handle, base_conn->self_handle, request_token);
		return TRUE;
	}

	if (require_new)
	{
		g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
					 "Already chatting with contact #%u in another channel", handle);
		goto error;
	}

	tp_channel_manager_emit_request_already_satisfied (self, request_token,
													   channel);
	return TRUE;

error:
	tp_channel_manager_emit_request_failed (self, request_token,
											error->domain, error->code, error->message);
	g_error_free (error);
	return TRUE;
}


static void
_im_channel_closed_cb (IdleIMChannel *chan,
					  gpointer user_data)
{
	IdleIMManager *self = IDLE_IM_MANAGER (user_data);
	IdleIMManagerPrivate *priv = IDLE_IM_MANAGER_GET_PRIVATE (self);
	TpBaseChannel *base = TP_BASE_CHANNEL (chan);

	tp_channel_manager_emit_channel_closed_for_object (self,
													   TP_EXPORTABLE_CHANNEL (chan));

	if (priv->channels)
	{
		TpHandle handle = tp_base_channel_get_target_handle (base);

		if (tp_base_channel_is_destroyed (base))
		{
			IDLE_DEBUG ("removing channel with handle %u", handle);
			g_hash_table_remove (priv->channels, GUINT_TO_POINTER (handle));
		} else {
			IDLE_DEBUG ("reopening channel with handle %u due to pending messages",
				handle);
			tp_channel_manager_emit_new_channel (self,
				(TpExportableChannel *) chan, NULL);
		}
	}
}


static IdleIMChannel *
_im_manager_new_channel (IdleIMManager *mgr,
						 TpHandle handle,
						 TpHandle initiator,
						 gpointer request)
{
	IdleIMManagerPrivate *priv = IDLE_IM_MANAGER_GET_PRIVATE (mgr);
	TpBaseConnection *base_connection = TP_BASE_CONNECTION (priv->conn);
	TpHandleRepoIface *handle_repo =
		tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);
	IdleIMChannel *chan;
	const gchar *name;
	GSList *requests = NULL;

	g_assert (g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle))
			  == NULL);

	name = tp_handle_inspect (handle_repo, handle);
	IDLE_DEBUG ("Requested channel for handle: %u (%s)", handle, name);

	chan = g_object_new (IDLE_TYPE_IM_CHANNEL,
						 "connection", priv->conn,
						 "handle", handle,
						 "initiator-handle", initiator,
						 "requested", handle != initiator,
						 NULL);
	tp_base_channel_register (TP_BASE_CHANNEL (chan));
	g_hash_table_insert (priv->channels, GUINT_TO_POINTER (handle), chan);

	if (request != NULL)
		requests = g_slist_prepend (requests, request);

	tp_channel_manager_emit_new_channel (mgr, TP_EXPORTABLE_CHANNEL (chan),
										 requests);

	g_slist_free (requests);

	g_signal_connect (chan, "closed", G_CALLBACK (_im_channel_closed_cb), mgr);

	return chan;
}

static void _im_manager_iface_init(gpointer g_iface, gpointer iface_data) {
	TpChannelManagerIface *iface = g_iface;

	iface->foreach_channel = _im_manager_foreach;
	iface->type_foreach_channel_class = _im_manager_type_foreach_class;
	iface->request_channel = _im_manager_request_channel;
	iface->create_channel = _im_manager_create_channel;
	iface->ensure_channel = _im_manager_ensure_channel;
}

static void
_im_manager_dispose (GObject *object)
{
	IdleIMManager *self = IDLE_IM_MANAGER (object);
	IdleIMManagerPrivate *priv = IDLE_IM_MANAGER_GET_PRIVATE (self);

	if (priv->dispose_has_run)
		return;

	priv->dispose_has_run = TRUE;

	_im_manager_close_all (self);

	if (G_OBJECT_CLASS (idle_im_manager_parent_class)->dispose)
		G_OBJECT_CLASS (idle_im_manager_parent_class)->dispose (object);
}

