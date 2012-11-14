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

#include "config.h"

#include "idle-muc-channel.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dbus/dbus-glib.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/handle.h>
#include <telepathy-glib/svc-channel.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_MUC
#include "idle-connection.h"
#include "idle-debug.h"
#include "idle-text.h"
#include "room-config.h"

static void _password_iface_init(gpointer, gpointer);
static void subject_iface_init(gpointer, gpointer);
static void destroyable_iface_init(gpointer, gpointer);

static void idle_muc_channel_send (GObject *obj, TpMessage *message, TpMessageSendingFlags flags);
static void idle_muc_channel_close (TpBaseChannel *base);

G_DEFINE_TYPE_WITH_CODE(IdleMUCChannel, idle_muc_channel, TP_TYPE_BASE_CHANNEL,
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP, tp_group_mixin_iface_init);
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL_INTERFACE_PASSWORD, _password_iface_init);
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL_TYPE_TEXT, tp_message_mixin_text_iface_init);
		G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MESSAGES, tp_message_mixin_messages_iface_init);
		G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_ROOM, NULL);
		G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_SUBJECT, subject_iface_init);
		G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_ROOM_CONFIG,
                                       tp_base_room_config_iface_init);
		G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_DESTROYABLE, destroyable_iface_init);
		)

/* property enum */
enum {
  PROP_0,

  PROP_SUBJECT,
  PROP_SUBJECT_ACTOR,
  PROP_SUBJECT_ACTOR_HANDLE,
  PROP_SUBJECT_TIMESTAMP,
  PROP_CAN_SET_SUBJECT,

  PROP_SERVER,
};

/* signal enum */
enum {
	JOIN_READY,
	LAST_SIGNAL
};

typedef enum {
	MUC_STATE_CREATED = 0,
	MUC_STATE_JOINING,
	MUC_STATE_NEED_PASSWORD,
	MUC_STATE_JOINED,
	MUC_STATE_PARTED
} IdleMUCState;

typedef enum {
	MODE_FLAG_CREATOR = 1,
	MODE_FLAG_OPERATOR_PRIVILEGE = 2,
	MODE_FLAG_VOICE_PRIVILEGE = 4,

	MODE_FLAG_ANONYMOUS = 8,
	MODE_FLAG_INVITE_ONLY = 16,
	MODE_FLAG_MODERATED= 32,
	MODE_FLAG_NO_OUTSIDE_MESSAGES = 64,
	MODE_FLAG_QUIET= 128,
	MODE_FLAG_PRIVATE= 256,
	MODE_FLAG_SECRET= 512,
	MODE_FLAG_SERVER_REOP= 1024,
	MODE_FLAG_TOPIC_ONLY_SETTABLE_BY_OPS = 2048,

	MODE_FLAG_KEY= 4096,
	MODE_FLAG_USER_LIMIT = 8192,

	MODE_FLAG_HALFOP_PRIVILEGE = 16384,

	LAST_MODE_FLAG_ENUM
} IRCChannelModeFlags;

#define MODE_FLAGS_OP \
   (MODE_FLAG_OPERATOR_PRIVILEGE | MODE_FLAG_HALFOP_PRIVILEGE)

typedef struct {
	IRCChannelModeFlags flags;
	guint limit;
	gchar *topic;
	gchar *key;
	gint64 topic_touched;
	TpHandle topic_toucher;
	const gchar *topic_toucher_id;
} IRCChannelModeState;

static const gchar *muc_channel_interfaces[] = {
	TP_IFACE_CHANNEL_INTERFACE_PASSWORD,
	TP_IFACE_CHANNEL_INTERFACE_GROUP,
	TP_IFACE_CHANNEL_INTERFACE_MESSAGES,
	TP_IFACE_CHANNEL_INTERFACE_ROOM,
	TP_IFACE_CHANNEL_INTERFACE_SUBJECT,
	TP_IFACE_CHANNEL_INTERFACE_ROOM_CONFIG,
	TP_IFACE_CHANNEL_INTERFACE_DESTROYABLE,
	NULL
};

static const gchar *ascii_muc_states[] = {
	"MUC_STATE_CREATED",
	"MUC_STATE_JOINING",
	"MUC_STATE_NEED_PASSWORD",
	"MUC_STATE_JOINED",
	"MUC_STATE_PARTED"
};

static gboolean add_member(GObject *gobj, TpHandle handle, const gchar *message, GError **error);
static gboolean remove_member(GObject *gobj, TpHandle handle, const gchar *message, GError **error);

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
struct _IdleMUCChannelPrivate {
	const gchar *channel_name;

	TpBaseRoomConfig *room_config;

	IdleMUCState state;

	IRCChannelModeState mode_state;
	gboolean can_set_topic;

	guint password_flags;

	DBusGMethodInvocation *passwd_ctx;

	/* NAMEREPLY MembersChanged aggregation */
	TpHandleSet *namereply_set;

	gboolean join_ready;

	gboolean dispose_has_run;
};

static void change_password_flags(IdleMUCChannel *chan, guint flag, gboolean state);

static void idle_muc_channel_init (IdleMUCChannel *obj) {
	IdleMUCChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
		IDLE_TYPE_MUC_CHANNEL, IdleMUCChannelPrivate);

	obj->priv = priv;
	priv->password_flags = 0;

	priv->state = MUC_STATE_CREATED;

	priv->can_set_topic = TRUE;

	priv->dispose_has_run = FALSE;

	priv->mode_state.topic_touched = G_MAXINT64;
}

static void idle_muc_channel_dispose (GObject *object);
static void idle_muc_channel_finalize (GObject *object);

static void
idle_muc_channel_constructed (GObject *obj)
{
	IdleMUCChannel *self = IDLE_MUC_CHANNEL (obj);
	TpBaseChannel *base = TP_BASE_CHANNEL (obj);
	IdleMUCChannelPrivate *priv = self->priv;
	TpBaseConnection *conn = tp_base_channel_get_connection (base);
	TpHandleRepoIface *room_handles = tp_base_connection_get_handles(conn, TP_HANDLE_TYPE_ROOM);
	TpHandleRepoIface *contact_handles = tp_base_connection_get_handles(conn, TP_HANDLE_TYPE_CONTACT);
	TpChannelTextMessageType types[] = {
			TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
			TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
			TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
	};
	const gchar * supported_content_types[] = {
			"text/plain",
			NULL
	};

	G_OBJECT_CLASS (idle_muc_channel_parent_class)->constructed (obj);

	priv->channel_name = tp_handle_inspect (room_handles, tp_base_channel_get_target_handle (base));
	g_assert (priv->channel_name != NULL);

	tp_base_channel_register (base);

	tp_group_mixin_init(obj, G_STRUCT_OFFSET(IdleMUCChannel, group), contact_handles, conn->self_handle);
	tp_group_mixin_change_flags(obj, TP_CHANNEL_GROUP_FLAG_PROPERTIES, 0);

	/* initialize message mixin */
	tp_message_mixin_init (obj, G_STRUCT_OFFSET (IdleMUCChannel, message_mixin),
			conn);
	tp_message_mixin_implement_sending (obj, idle_muc_channel_send,
			G_N_ELEMENTS (types), types, 0,
			TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_FAILURES,
			supported_content_types);

	if (tp_base_channel_is_requested (base)) {
		/* Add ourself to 'remote-pending' while we are joining the channel */
		TpIntset *remote;
		TpHandle initiator = tp_base_channel_get_initiator (base);

		g_assert (initiator == conn->self_handle);

		remote = tp_intset_new_containing (initiator);
		tp_group_mixin_change_members (obj, "", NULL, NULL, NULL, remote,
			initiator, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
		tp_intset_destroy (remote);
	}

        priv->room_config =
          (TpBaseRoomConfig *) idle_room_config_new ((TpBaseChannel *) self);
}

static void
idle_muc_channel_get_property (
    GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  IdleMUCChannel *self = IDLE_MUC_CHANNEL (object);
  IdleMUCChannelPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_SUBJECT:
        g_value_set_string (value, priv->mode_state.topic);
        break;
      case PROP_SUBJECT_ACTOR:
        g_value_set_string (value, priv->mode_state.topic_toucher_id);
        break;
      case PROP_SUBJECT_ACTOR_HANDLE:
        g_value_set_uint (value, priv->mode_state.topic_toucher);
        break;
      case PROP_SUBJECT_TIMESTAMP:
        g_value_set_int64 (value, priv->mode_state.topic_touched);
        break;
      case PROP_CAN_SET_SUBJECT:
        g_value_set_boolean (value, priv->can_set_topic);
        break;
      case PROP_SERVER:
        g_value_set_static_string (value, "");
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static gchar *
idle_muc_channel_get_path_suffix (
    TpBaseChannel *chan)
{
  return g_strdup_printf("MucChannel%u",
      tp_base_channel_get_target_handle (chan));
}

static void
idle_muc_channel_fill_immutable_properties (
    TpBaseChannel *chan,
    GHashTable *properties)
{
  TpBaseChannelClass *parent_class =
      TP_BASE_CHANNEL_CLASS (idle_muc_channel_parent_class);

  parent_class->fill_immutable_properties (chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessagePartSupportFlags",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "DeliveryReportingSupport",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "SupportedContentTypes",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessageTypes",
      TP_IFACE_CHANNEL_INTERFACE_ROOM, "RoomName",
      TP_IFACE_CHANNEL_INTERFACE_ROOM, "Server",
      NULL);
}

static GPtrArray *
idle_muc_channel_get_interfaces (TpBaseChannel *self)
{
  TpBaseChannelClass *parent_class =
      TP_BASE_CHANNEL_CLASS (idle_muc_channel_parent_class);
  GPtrArray *interfaces = parent_class->get_interfaces (self);
  const gchar **interface;

  for (interface = muc_channel_interfaces; *interface != NULL; interface++)
    g_ptr_array_add (interfaces, (gchar *) *interface);

  return interfaces;
}

static void idle_muc_channel_class_init (IdleMUCChannelClass *idle_muc_channel_class) {
	GObjectClass *object_class = G_OBJECT_CLASS (idle_muc_channel_class);
	TpBaseChannelClass *base_channel_class = TP_BASE_CHANNEL_CLASS (idle_muc_channel_class);
	GParamSpec *param_spec;
	static TpDBusPropertiesMixinPropImpl room_props[] = {
		{ "RoomName", "target-id", NULL },
		{ "Server", "server", NULL },
		{ NULL },
	};
	static TpDBusPropertiesMixinPropImpl subject_props[] = {
		{ "Subject", "subject", NULL },
		{ "Actor", "subject-actor", NULL },
		{ "ActorHandle", "subject-actor-handle", NULL },
		{ "Timestamp", "subject-timestamp", NULL },
		{ "CanSet", "can-set-subject", NULL },
		{ NULL },
	};

	g_type_class_add_private (idle_muc_channel_class, sizeof (IdleMUCChannelPrivate));

	object_class->constructed = idle_muc_channel_constructed;
	object_class->get_property = idle_muc_channel_get_property;
	object_class->dispose = idle_muc_channel_dispose;
	object_class->finalize = idle_muc_channel_finalize;

	base_channel_class->channel_type = TP_IFACE_CHANNEL_TYPE_TEXT;
	base_channel_class->target_handle_type = TP_HANDLE_TYPE_ROOM;

	base_channel_class->close = idle_muc_channel_close;
	base_channel_class->fill_immutable_properties = idle_muc_channel_fill_immutable_properties;
	base_channel_class->get_object_path_suffix = idle_muc_channel_get_path_suffix;
	base_channel_class->get_interfaces = idle_muc_channel_get_interfaces;

	param_spec = g_param_spec_string (
		"server", "Room.Server", "always empty",
		"", G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_SERVER,
		param_spec);

	param_spec = g_param_spec_string (
		"subject", "Subject.Subject", "(aka topic)",
		NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_SUBJECT,
		param_spec);

	param_spec = g_param_spec_string (
		"subject-actor", "Subject.Actor", "who set the topic",
		NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_SUBJECT_ACTOR,
		param_spec);

	param_spec = g_param_spec_uint (
		"subject-actor-handle", "Subject.ActorHandle",
		"who set the topic (who hoo hoo, hoo hoo)",
		0, G_MAXUINT, 0,
		G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_SUBJECT_ACTOR_HANDLE,
		param_spec);

	param_spec = g_param_spec_int64 (
		"subject-timestamp", "Subject.Timestamp", "when they set it",
		G_MININT64, G_MAXINT64, 0,
		G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_SUBJECT_TIMESTAMP,
		param_spec);

	param_spec = g_param_spec_boolean (
		"can-set-subject", "Subject.CanSet", "can we change the topic",
		TRUE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_CAN_SET_SUBJECT,
		param_spec);

	signals[JOIN_READY] = g_signal_new("join-ready", G_OBJECT_CLASS_TYPE(idle_muc_channel_class), G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);

	tp_group_mixin_class_init(object_class, G_STRUCT_OFFSET(IdleMUCChannelClass, group_class), add_member, remove_member);
	tp_message_mixin_init_dbus_properties (object_class);

	tp_group_mixin_init_dbus_properties (object_class);
	tp_group_mixin_class_allow_self_removal (object_class);

        tp_base_room_config_register_class (base_channel_class);

	tp_dbus_properties_mixin_implement_interface (object_class,
		TP_IFACE_QUARK_CHANNEL_INTERFACE_ROOM,
		tp_dbus_properties_mixin_getter_gobject_properties, NULL,
		room_props);
	tp_dbus_properties_mixin_implement_interface (object_class,
		TP_IFACE_QUARK_CHANNEL_INTERFACE_SUBJECT,
		tp_dbus_properties_mixin_getter_gobject_properties, NULL,
		subject_props);
}

void idle_muc_channel_dispose (GObject *object) {
	IdleMUCChannel *self = IDLE_MUC_CHANNEL (object);
	IdleMUCChannelPrivate *priv = self->priv;

	if (priv->dispose_has_run)
		return;

	priv->dispose_has_run = TRUE;

        tp_clear_object (&priv->room_config);

	if (G_OBJECT_CLASS (idle_muc_channel_parent_class)->dispose)
		G_OBJECT_CLASS (idle_muc_channel_parent_class)->dispose (object);
}

void idle_muc_channel_finalize (GObject *object) {
	IdleMUCChannel *self = IDLE_MUC_CHANNEL (object);
	IdleMUCChannelPrivate *priv = self->priv;

	if (priv->mode_state.topic)
		g_free(priv->mode_state.topic);

	if (priv->mode_state.key)
		g_free(priv->mode_state.key);

	if (priv->namereply_set)
		tp_handle_set_destroy(priv->namereply_set);

	tp_group_mixin_finalize(object);
	tp_message_mixin_finalize (object);

	G_OBJECT_CLASS (idle_muc_channel_parent_class)->finalize (object);
}

IdleMUCChannel *
idle_muc_channel_new (
    IdleConnection *conn,
    TpHandle handle,
    TpHandle initiator,
    gboolean requested) {
	return g_object_new(IDLE_TYPE_MUC_CHANNEL,
		"connection", conn,
		"handle", handle,
		"initiator-handle", initiator,
		"requested", requested,
		NULL);
}

static void provide_password_reply(IdleMUCChannel *chan, gboolean success) {
	IdleMUCChannelPrivate *priv;

	g_assert(chan != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(chan));

	priv = chan->priv;

	if (priv->passwd_ctx != NULL) {
		tp_svc_channel_interface_password_return_from_provide_password(priv->passwd_ctx, success);
		priv->passwd_ctx = NULL;
	} else {
		IDLE_DEBUG("don't have a ProvidePassword context to return with! (%s, aka %u)",
			priv->channel_name, tp_base_channel_get_target_handle (TP_BASE_CHANNEL (chan)));
	}

	if (success) {
		change_password_flags(chan, TP_CHANNEL_PASSWORD_FLAG_PROVIDE, 0);
	}
}

static void change_state(IdleMUCChannel *obj, IdleMUCState state) {
	IdleMUCChannelPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = obj->priv;

	if ((state > MUC_STATE_JOINING) && (!priv->join_ready)) {
		IDLE_DEBUG("emitting join-ready");
		g_signal_emit(obj, signals[JOIN_READY], 0, MUC_CHANNEL_JOIN_ERROR_NONE);
		priv->join_ready = TRUE;
	}

	if (priv->state == MUC_STATE_NEED_PASSWORD && state == MUC_STATE_JOINED) {
		change_password_flags(obj, TP_CHANNEL_PASSWORD_FLAG_PROVIDE, FALSE);
		provide_password_reply(obj, TRUE);
	}

	if (priv->state == MUC_STATE_NEED_PASSWORD && state == MUC_STATE_NEED_PASSWORD) {
		provide_password_reply(obj, FALSE);
	}

	if (priv->state < MUC_STATE_NEED_PASSWORD && state == MUC_STATE_NEED_PASSWORD) {
		change_password_flags(obj, TP_CHANNEL_PASSWORD_FLAG_PROVIDE, TRUE);
	}

	priv->state = state;

	IDLE_DEBUG("IdleMUCChannel %s changed to state %s", priv->channel_name, ascii_muc_states[state]);
}

gboolean idle_muc_channel_is_ready(IdleMUCChannel *obj) {
	IdleMUCChannelPrivate *priv;

	g_return_val_if_fail(obj != NULL, FALSE);
	g_return_val_if_fail(IDLE_IS_MUC_CHANNEL(obj), FALSE);

	priv = obj->priv;

	return priv->join_ready;
}

static void
idle_muc_channel_update_can_set_topic (
    IdleMUCChannel *self,
    gboolean can_set_topic)
{
  IdleMUCChannelPrivate *priv = self->priv;
  static const char *changed[] = { "CanSet", NULL };

  IDLE_DEBUG ("was %s, now %s",
      priv->can_set_topic ? "TRUE" : "FALSE",
      can_set_topic ? "TRUE" : "FALSE");

  if (!can_set_topic == !priv->can_set_topic)
    return;

  priv->can_set_topic = !!can_set_topic;
  tp_dbus_properties_mixin_emit_properties_changed (G_OBJECT (self),
      TP_IFACE_CHANNEL_INTERFACE_SUBJECT, changed);
}

static void change_mode_state(IdleMUCChannel *obj, guint add, guint remove) {
	IdleMUCChannelPrivate *priv;
	IRCChannelModeFlags flags;
	guint group_add = 0, group_remove = 0;
	guint combined;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	remove &= ~add;

	priv = obj->priv;
	flags = priv->mode_state.flags;

	IDLE_DEBUG("got %x, %x", add, remove);

	add &= ~flags;
	remove &= flags;

	IDLE_DEBUG("operation %x, %x", add, remove);

	flags |= add;
	flags &= ~remove;

	combined = add | remove;

	if (add & MODE_FLAG_INVITE_ONLY) {
		if (!(flags & (MODE_FLAG_OPERATOR_PRIVILEGE | MODE_FLAG_HALFOP_PRIVILEGE)))
			group_remove |= TP_CHANNEL_GROUP_FLAG_CAN_ADD;
	} else if (remove & MODE_FLAG_INVITE_ONLY) {
		group_add |= TP_CHANNEL_GROUP_FLAG_CAN_ADD;
	}

	if (combined & (MODE_FLAG_OPERATOR_PRIVILEGE | MODE_FLAG_HALFOP_PRIVILEGE)) {

		if (add & (MODE_FLAG_OPERATOR_PRIVILEGE | MODE_FLAG_HALFOP_PRIVILEGE)) {
			group_add |= TP_CHANNEL_GROUP_FLAG_CAN_ADD | TP_CHANNEL_GROUP_FLAG_CAN_REMOVE | TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE;

			tp_base_room_config_set_can_update_configuration (priv->room_config, TRUE);

			if (flags & MODE_FLAG_TOPIC_ONLY_SETTABLE_BY_OPS)
				idle_muc_channel_update_can_set_topic (obj, TRUE);
		} else if (remove & (MODE_FLAG_OPERATOR_PRIVILEGE | MODE_FLAG_HALFOP_PRIVILEGE)) {
			group_remove |= TP_CHANNEL_GROUP_FLAG_CAN_REMOVE | TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE;

			tp_base_room_config_set_can_update_configuration (priv->room_config, FALSE);

			if (flags & MODE_FLAG_INVITE_ONLY)
				group_remove |= TP_CHANNEL_GROUP_FLAG_CAN_ADD;

			if (flags & MODE_FLAG_TOPIC_ONLY_SETTABLE_BY_OPS)
				idle_muc_channel_update_can_set_topic (obj, FALSE);
		}
	} else if ((combined & MODE_FLAG_TOPIC_ONLY_SETTABLE_BY_OPS)
			&& !(priv->mode_state.flags & MODE_FLAGS_OP)) {
		/* We're not ops and the MODE_FLAG_TOPIC_ONLY_SETTABLE_BY_OPS flag was
		 * changed */
		if (add & MODE_FLAG_TOPIC_ONLY_SETTABLE_BY_OPS)
			idle_muc_channel_update_can_set_topic (obj, FALSE);
		else
			idle_muc_channel_update_can_set_topic (obj, TRUE);
	}

	for (int i = 1; i < LAST_MODE_FLAG_ENUM; i <<= 1) {
		if (combined & i) {
                  gboolean value = (add & i) ? TRUE : FALSE;

                  switch (i)
                    {
                    case MODE_FLAG_INVITE_ONLY:
                      /* invite-only */
                      g_object_set (priv->room_config,
                          "invite-only", value,
                          NULL);
                      break;

                    case MODE_FLAG_MODERATED:
                      /* moderated */
                      g_object_set (priv->room_config,
                          "moderated", value,
                          NULL);
                      break;

                    case MODE_FLAG_USER_LIMIT:
                      /* limit */
                      g_object_set (priv->room_config,
                          "limit", value ? priv->mode_state.limit : 0,
                          NULL);
                      break;

                    case MODE_FLAG_KEY:
                      /* password-protected */
                      {
                        g_object_set (priv->room_config,
                            "password-protected", value,
                            NULL);

                        /* if value==TRUE we have a password! */
                        if (value)
                          {
                            g_object_set (priv->room_config,
                                "password", priv->mode_state.key,
                                NULL);
                          }
                      }
                      break;

                    case MODE_FLAG_PRIVATE:
                    case MODE_FLAG_SECRET:
                      /* private */
                      g_object_set (priv->room_config,
                          "private", value,
                          NULL);
                      break;

                    default:
                      break;
                    }

		}
	}

	tp_group_mixin_change_flags((GObject *)obj, group_add, group_remove);

        tp_base_room_config_emit_properties_changed (priv->room_config);

	priv->mode_state.flags = flags;

	IDLE_DEBUG("changed to %x", flags);
}

static void change_password_flags(IdleMUCChannel *obj, guint flag, gboolean state) {
	IdleMUCChannelPrivate *priv;
	guint add = 0, remove = 0;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = obj->priv;

	if (state) {
		add = (~(priv->password_flags)) & flag;
		priv->password_flags |= flag;
	} else {
		remove = priv->password_flags & flag;
		priv->password_flags &= ~flag;
	}

	if (add | remove) {
		IDLE_DEBUG("emitting PASSWORD_FLAGS_CHANGED with %u %u", add, remove);
		tp_svc_channel_interface_password_emit_password_flags_changed((TpSvcChannelInterfacePassword *)(obj), add, remove);
	}
}

gboolean idle_muc_channel_receive(IdleMUCChannel *chan, TpChannelTextMessageType type, TpHandle sender, const gchar *text) {
	TpBaseConnection *base_conn = tp_base_channel_get_connection (TP_BASE_CHANNEL (chan));

	return idle_text_received (G_OBJECT (chan), base_conn, type, text, sender);
}

static void
send_command (
    IdleMUCChannel *self,
    const gchar *cmd)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);

  idle_connection_send (IDLE_CONNECTION (base_conn), cmd);
}

static void send_mode_query_request(IdleMUCChannel *chan) {
	IdleMUCChannelPrivate *priv;
	gchar cmd[IRC_MSG_MAXLEN + 2];

	g_assert(chan != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(chan));

	priv = chan->priv;

	g_snprintf(cmd, IRC_MSG_MAXLEN + 2, "MODE %s", priv->channel_name);

	send_command (chan, cmd);
}

void idle_muc_channel_join(IdleMUCChannel *chan, TpHandle joiner) {
	IdleMUCChannelPrivate *priv = chan->priv;
	TpBaseConnection *base_conn = tp_base_channel_get_connection (
		TP_BASE_CHANNEL (chan));
	TpIntset *set;

	set = tp_intset_new();
	tp_intset_add(set, joiner);

	if (joiner == base_conn->self_handle) {
		/* woot we managed to get into a channel, great */
		change_state(chan, MUC_STATE_JOINED);
		tp_group_mixin_change_members((GObject *)(chan), NULL, set, NULL, NULL, NULL, joiner, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
		tp_group_mixin_change_flags((GObject *)(chan),
			TP_CHANNEL_GROUP_FLAG_CAN_ADD |
			TP_CHANNEL_GROUP_FLAG_MESSAGE_DEPART,
			0);

		send_mode_query_request(chan);

		if (priv->channel_name[0] == '+')
			/* according to IRC specs, PLUS channels do not support channel modes and alway have only +t set, so we work with that. */
			change_mode_state(chan, MODE_FLAG_TOPIC_ONLY_SETTABLE_BY_OPS, 0);
	} else {
		tp_group_mixin_change_members((GObject *)(chan), NULL, set, NULL, NULL, NULL, joiner, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
	}

	IDLE_DEBUG("member joined with handle %u", joiner);

	tp_intset_destroy(set);
}

static void _network_member_left(IdleMUCChannel *chan, TpHandle leaver, TpHandle actor, const gchar *message, TpChannelGroupChangeReason reason) {
	TpBaseChannel *base = TP_BASE_CHANNEL (chan);
	TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
	TpIntset *set = tp_intset_new();

	tp_intset_add(set, leaver);
	tp_group_mixin_change_members((GObject *) chan, message, NULL, set, NULL, NULL, actor, reason);

	if (leaver == base_conn->self_handle) {
		change_state(chan, MUC_STATE_PARTED);

		if (!tp_base_channel_is_destroyed (base)) {
			tp_base_channel_destroyed (base);
		}
	}

	tp_intset_destroy(set);
}

void idle_muc_channel_part(IdleMUCChannel *chan, TpHandle leaver, const gchar *message) {
	_network_member_left(chan, leaver, leaver, message, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
}

void idle_muc_channel_kick(IdleMUCChannel *chan, TpHandle kicked, TpHandle kicker, const gchar *message) {
	_network_member_left(chan, kicked, kicker, message, TP_CHANNEL_GROUP_CHANGE_REASON_KICKED);
}

void idle_muc_channel_quit(IdleMUCChannel *chan, TpHandle quitter, const gchar *message) {
	_network_member_left(chan, quitter, quitter, message, TP_CHANNEL_GROUP_CHANGE_REASON_OFFLINE);
}

void idle_muc_channel_invited(IdleMUCChannel *chan, TpHandle inviter) {
	TpBaseConnection *base_conn =
		tp_base_channel_get_connection (TP_BASE_CHANNEL (chan));
	TpIntset *add = tp_intset_new();
	TpIntset *local = tp_intset_new();

	tp_intset_add(add, inviter);
	tp_intset_add(local, base_conn->self_handle);

	tp_group_mixin_change_members((GObject *)(chan), NULL, add, NULL, local, NULL, inviter, TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);

	tp_intset_destroy(add);
	tp_intset_destroy(local);
}

void idle_muc_channel_namereply(IdleMUCChannel *chan, GValueArray *args) {
	IdleMUCChannelPrivate *priv = chan->priv;
	TpBaseChannel *base = TP_BASE_CHANNEL (chan);
	TpBaseConnection *base_conn = tp_base_channel_get_connection (base);

	if (!priv->namereply_set)
		priv->namereply_set = tp_handle_set_new(tp_base_connection_get_handles(base_conn, TP_HANDLE_TYPE_CONTACT));

	for (guint i = 1; (i + 1) < args->n_values; i += 2) {
		TpHandle handle = g_value_get_uint(g_value_array_get_nth(args, i));
#if GLIB_CHECK_VERSION(2, 31, 0)
		gchar modechar = g_value_get_schar(g_value_array_get_nth(args, i + 1));
#else
		gchar modechar = g_value_get_char(g_value_array_get_nth(args, i + 1));
#endif

		if (handle == base_conn->self_handle) {
			guint remove = MODE_FLAG_OPERATOR_PRIVILEGE | MODE_FLAG_VOICE_PRIVILEGE | MODE_FLAG_HALFOP_PRIVILEGE;
			guint add = 0;

			switch (modechar) {
				case '@':
					add |= MODE_FLAG_OPERATOR_PRIVILEGE;
					break;

				case '&':
					add |= MODE_FLAG_OPERATOR_PRIVILEGE;
					break;

				case '+':
					add |= MODE_FLAG_VOICE_PRIVILEGE;
					break;

				default:
					break;
			}

			remove &= ~add;
			change_mode_state(chan, add, remove);
		}

		tp_handle_set_add(priv->namereply_set, handle);
	}
}

void idle_muc_channel_namereply_end(IdleMUCChannel *chan) {
	IdleMUCChannelPrivate *priv = chan->priv;
	TpBaseChannel *base = TP_BASE_CHANNEL (chan);
	TpBaseConnection *base_conn = tp_base_channel_get_connection (base);

	if (!priv->namereply_set) {
		IDLE_DEBUG("no NAMEREPLY received before NAMEREPLY_END");
		return;
	}

	idle_connection_emit_queued_aliases_changed(IDLE_CONNECTION (base_conn));

	tp_group_mixin_change_members((GObject *) chan, NULL, tp_handle_set_peek(priv->namereply_set), NULL, NULL, NULL, 0, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

	tp_handle_set_destroy(priv->namereply_set);
	priv->namereply_set = NULL;
}

static guint _modechar_to_modeflag(gchar modechar) {
	switch (modechar) {
		case 'o':
			return MODE_FLAG_OPERATOR_PRIVILEGE;
		case 'h':
			return MODE_FLAG_HALFOP_PRIVILEGE;
		case 'v':
			return MODE_FLAG_VOICE_PRIVILEGE;
		default:
			return 0;
	}
}

void idle_muc_channel_mode(IdleMUCChannel *chan, GValueArray *args) {
	IdleMUCChannelPrivate *priv = chan->priv;
	TpBaseChannel *base = TP_BASE_CHANNEL (chan);
	TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
	TpHandleRepoIface *handles = tp_base_connection_get_handles(base_conn, TP_HANDLE_TYPE_CONTACT);

        tp_base_room_config_set_retrieved (priv->room_config);

	for (guint i = 1; i < args->n_values; i++) {
		const gchar *modes = g_value_get_string(g_value_array_get_nth(args, i));
		gchar operation = modes[0];
		guint mode_accum = 0;
		guint limit = 0;
		gchar *key = NULL;

		if ((operation != '+') && (operation != '-'))
			continue;

		for (; *modes != '\0'; modes++) {
			switch (*modes) {
				case 'o':
				case 'h':
				case 'v':
					if ((i + 1) < args->n_values) {
						TpHandle handle = tp_handle_ensure(handles, g_value_get_string(g_value_array_get_nth(args, ++i)), NULL, NULL);

						if (handle == base_conn->self_handle) {
							IDLE_DEBUG("got MODE '%c' concerning us", *modes);
							mode_accum |= _modechar_to_modeflag(*modes);
						}
					}
					break;

				case 'l':
					if (operation == '+') {
						if ((i + 1) < args->n_values) {
							const gchar *limit_str = g_value_get_string(g_value_array_get_nth(args, ++i));
							gchar *endptr;
							guint maybe_limit = strtol(limit_str, &endptr, 10);

							if (endptr != limit_str)
								limit = maybe_limit;
						}
					}

					mode_accum |= MODE_FLAG_USER_LIMIT;
					break;

				case 'k':
					if (operation == '+') {
						if ((i + 1) < args->n_values) {
							g_free(key);
							key = g_strdup(g_value_get_string(g_value_array_get_nth(args, ++i)));
						}
					}

					mode_accum |= MODE_FLAG_KEY;
					break;

				case 'a':
					mode_accum |= MODE_FLAG_ANONYMOUS;
					break;

				case 'i':
					mode_accum |= MODE_FLAG_INVITE_ONLY;
					break;

				case 'm':
					mode_accum |= MODE_FLAG_MODERATED;
					break;

				case 'n':
					mode_accum |= MODE_FLAG_NO_OUTSIDE_MESSAGES;
					break;

				case 'q':
					mode_accum |= MODE_FLAG_QUIET;
					break;

				case 'p':
					mode_accum |= MODE_FLAG_PRIVATE;
					break;

				case 's':
					mode_accum |= MODE_FLAG_SECRET;
					break;

				case 'r':
					mode_accum |= MODE_FLAG_SERVER_REOP;
					break;

				case 't':
					mode_accum |= MODE_FLAG_TOPIC_ONLY_SETTABLE_BY_OPS;
					break;

				case '+':
				case '-':
					if (operation != *modes) {
						if (mode_accum & MODE_FLAG_KEY) {
							g_free(priv->mode_state.key);
							priv->mode_state.key = key;
						}

						if (mode_accum & MODE_FLAG_USER_LIMIT)
							priv->mode_state.limit = limit;

						if (operation == '+')
							change_mode_state(chan, mode_accum, 0);
						else
							change_mode_state(chan, 0, mode_accum);

						operation = *modes;
						mode_accum = 0;
					}

					break;

				default:
					IDLE_DEBUG("did not understand mode identifier %c", *modes);
					break;
			}
		}

		if (mode_accum & MODE_FLAG_KEY) {
			g_free(priv->mode_state.key);
			priv->mode_state.key = key;
		}

		if (mode_accum & MODE_FLAG_USER_LIMIT)
			priv->mode_state.limit = limit;

		if (operation == '+')
			change_mode_state(chan, mode_accum, 0);
		else
			change_mode_state(chan, 0, mode_accum);
	}
}

void
idle_muc_channel_topic (
    IdleMUCChannel *self,
    const char *topic)
{
  IdleMUCChannelPrivate *priv = self->priv;

  idle_muc_channel_topic_full (self,
      priv->mode_state.topic_toucher,
      priv->mode_state.topic_touched,
      topic);
}

void
idle_muc_channel_topic_touch (
    IdleMUCChannel *self,
    const TpHandle toucher,
    const gint64 timestamp)
{
  IdleMUCChannelPrivate *priv = self->priv;

  idle_muc_channel_topic_full (self, toucher, timestamp,
      priv->mode_state.topic);
}

void
idle_muc_channel_topic_full (
    IdleMUCChannel *self,
    const TpHandle toucher,
    const gint64 timestamp,
    const gchar *topic)
{
  IdleMUCChannelPrivate *priv = self->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  TpHandleRepoIface *handles = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);
  guint i = 0;
  static const gchar *changed[] = { NULL, NULL, NULL, NULL, NULL };

  /* Don't blow up if we pass the existing topic pointer in. */
  if (priv->mode_state.topic != topic)
    {
      g_free (priv->mode_state.topic);
      priv->mode_state.topic = g_strdup (topic);
      changed[i++] = "Subject";
    }

  if (priv->mode_state.topic_touched != timestamp)
    {
      priv->mode_state.topic_touched = timestamp;
      changed[i++] = "Timestamp";
    }

  if (priv->mode_state.topic_toucher != toucher)
    {
      priv->mode_state.topic_toucher = toucher;
      changed[i++] = "Actor";
      changed[i++] = "ActorHandle";
    }

  if (toucher != 0)
    priv->mode_state.topic_toucher_id = tp_handle_inspect (handles, toucher);
  else
    priv->mode_state.topic_toucher_id = "";

  tp_dbus_properties_mixin_emit_properties_changed (G_OBJECT (self),
      TP_IFACE_CHANNEL_INTERFACE_SUBJECT, changed);
}

void
idle_muc_channel_topic_unset (
    IdleMUCChannel *self)
{
  idle_muc_channel_topic_full (self, 0, G_MAXINT64, "");
}

void idle_muc_channel_badchannelkey(IdleMUCChannel *chan) {
	change_state(chan, MUC_STATE_NEED_PASSWORD);
}

void idle_muc_channel_join_error(IdleMUCChannel *chan, IdleMUCChannelJoinError err) {
	IdleMUCChannelPrivate *priv;

	g_assert(chan != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(chan));

	priv = chan->priv;

	if (!priv->join_ready) {
		priv->join_ready = TRUE;

		g_signal_emit(chan, signals[JOIN_READY], 0, err);
	} else {
		IDLE_DEBUG("already emitted JOIN_READY! (current err %u)", err);
	}
}

void idle_muc_channel_rename(IdleMUCChannel *chan, TpHandle old_handle, TpHandle new_handle) {
	TpIntset *add = tp_intset_new();
	TpIntset *remove = tp_intset_new();
	TpIntset *local = tp_intset_new();
	TpIntset *remote = tp_intset_new();

	if (old_handle == chan->group.self_handle)
		tp_group_mixin_change_self_handle((GObject *) chan, new_handle);

	tp_intset_add(remove, old_handle);

	if (tp_handle_set_is_member(chan->group.members, old_handle))
		tp_intset_add(add, new_handle);
	else if (tp_handle_set_is_member(chan->group.local_pending, old_handle))
		tp_intset_add(local, new_handle);
	else if (tp_handle_set_is_member(chan->group.remote_pending, old_handle))
		tp_intset_add(remote, new_handle);
	else
		goto cleanup;

	tp_group_mixin_change_members((GObject *) chan, NULL, add, remove, local, remote, new_handle, TP_CHANNEL_GROUP_CHANGE_REASON_RENAMED);

cleanup:

	tp_intset_destroy(add);
	tp_intset_destroy(remove);
	tp_intset_destroy(local);
	tp_intset_destroy(remote);
}

static void send_join_request(IdleMUCChannel *obj, const gchar *password) {
	IdleMUCChannelPrivate *priv;
	gchar cmd[IRC_MSG_MAXLEN + 1];

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = obj->priv;

	if (password)
		g_snprintf(cmd, IRC_MSG_MAXLEN + 1, "JOIN %s %s", priv->channel_name, password);
	else
		g_snprintf(cmd, IRC_MSG_MAXLEN + 1, "JOIN %s", priv->channel_name);

	send_command (obj, cmd);
}

void idle_muc_channel_join_attempt(IdleMUCChannel *obj) {
	send_join_request(obj, NULL);
}

static gboolean send_invite_request(IdleMUCChannel *obj, TpHandle handle, GError **error) {
	IdleMUCChannelPrivate *priv;
	TpBaseChannel *base = TP_BASE_CHANNEL (obj);
	TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
	gchar cmd[IRC_MSG_MAXLEN + 1];
	const gchar *nick;

	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = obj->priv;

	nick = tp_handle_inspect(tp_base_connection_get_handles(base_conn, TP_HANDLE_TYPE_CONTACT), handle);

	if ((nick == NULL) || (nick[0] == '\0')) {
		IDLE_DEBUG("invalid handle %u passed", handle);

		g_set_error(error, TP_ERROR, TP_ERROR_INVALID_HANDLE, "invalid handle %u passed", handle);

		return FALSE;
	}

	g_snprintf(cmd, IRC_MSG_MAXLEN + 1, "INVITE %s %s", nick, priv->channel_name);

	send_command (obj, cmd);

	return TRUE;
}

static gboolean send_kick_request(IdleMUCChannel *obj, TpHandle handle, const gchar *msg, GError **error) {
	IdleMUCChannelPrivate *priv;
	TpBaseChannel *base = TP_BASE_CHANNEL (obj);
	TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
	gchar cmd[IRC_MSG_MAXLEN + 1];
	const gchar *nick;

	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = obj->priv;

	nick = tp_handle_inspect(tp_base_connection_get_handles(base_conn, TP_HANDLE_TYPE_CONTACT), handle);

	if ((nick == NULL) || (nick[0] == '\0')) {
		IDLE_DEBUG("invalid handle %u passed", handle);

		g_set_error(error, TP_ERROR, TP_ERROR_INVALID_HANDLE, "invalid handle %u passed", handle);

		return FALSE;
	}

	if (msg != NULL) {
		g_snprintf(cmd, IRC_MSG_MAXLEN + 1, "KICK %s %s :%s", priv->channel_name, nick, msg);
	} else {
		g_snprintf(cmd, IRC_MSG_MAXLEN + 1, "KICK %s %s", priv->channel_name, nick);
	}

	send_command (obj, cmd);

	return TRUE;
}

static gboolean add_member(GObject *gobj, TpHandle handle, const gchar *message, GError **error) {
	IdleMUCChannel *obj = IDLE_MUC_CHANNEL(gobj);
	IdleMUCChannelPrivate *priv = obj->priv;
	TpBaseChannel *base = TP_BASE_CHANNEL (obj);
	TpBaseConnection *base_conn = tp_base_channel_get_connection (base);

	if (handle == base_conn->self_handle) {
		if (tp_handle_set_is_member(obj->group.members, handle) || tp_handle_set_is_member(obj->group.remote_pending, handle)) {
			GError *e = g_error_new (TP_ERROR, TP_ERROR_NOT_AVAILABLE,
				"we are already a member of or trying to join %s", priv->channel_name);
			IDLE_DEBUG ("%s", e->message);
			g_propagate_error (error, e);
			return FALSE;
		} else {
			TpIntset *add_set = tp_intset_new();

			send_join_request(obj, NULL);

			change_state(obj, MUC_STATE_JOINING);

			tp_intset_add(add_set, handle);

			tp_group_mixin_change_members(gobj, message, NULL, NULL, NULL, add_set, handle, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
		}
	} else {
		if (tp_handle_set_is_member(obj->group.members, handle) || tp_handle_set_is_member(obj->group.remote_pending, handle)) {
			GError *e = g_error_new (TP_ERROR, TP_ERROR_NOT_AVAILABLE,
				"the requested contact (handle %u) to be added "
				"to the room (%s) is already a member of, or "
				"has already been invited to join, the room",
				handle, priv->channel_name);
			IDLE_DEBUG ("%s", e->message);
			g_propagate_error (error, e);
			return FALSE;
		} else {
			GError *invite_error;
			TpIntset *add_set = tp_intset_new();

			if (!send_invite_request(obj, handle, &invite_error)) {
				*error = invite_error;

				return FALSE;
			}

			tp_intset_add(add_set, handle);

			tp_group_mixin_change_members(gobj, NULL, NULL, NULL, NULL, add_set, base_conn->self_handle, TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);
		}
	}

	return TRUE;
}

static void part_from_channel(IdleMUCChannel *obj, const gchar *msg) {
	IdleMUCChannelPrivate *priv;
	gchar cmd[IRC_MSG_MAXLEN + 1];

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = obj->priv;

	if (msg != NULL) {
		g_snprintf(cmd, IRC_MSG_MAXLEN + 1, "PART %s :%s", priv->channel_name, msg);
	} else {
		g_snprintf(cmd, IRC_MSG_MAXLEN + 1, "PART %s", priv->channel_name);
	}

	send_command(obj, cmd);
}

static gboolean remove_member(GObject *gobj, TpHandle handle, const gchar *message, GError **error) {
	IdleMUCChannel *obj = IDLE_MUC_CHANNEL(gobj);
	TpBaseChannel *base = TP_BASE_CHANNEL (obj);
	TpBaseConnection *base_conn = tp_base_channel_get_connection (base);

	if (handle == base_conn->self_handle) {
		part_from_channel(obj, message);
		return TRUE;
	}

	if (!tp_handle_set_is_member(obj->group.members, handle)) {
		IDLE_DEBUG("handle %u not a current member!", handle);

		g_set_error(error, TP_ERROR, TP_ERROR_NOT_AVAILABLE, "handle %u is not a current member of the channel", handle);

		return FALSE;
	}

	if (!send_kick_request(obj, handle, message, error)) {
		IDLE_DEBUG("send_kick_request failed: %s", (*error)->message);
		return FALSE;
	}

	return TRUE;
}

static void
idle_muc_channel_close (
    TpBaseChannel *base)
{
	IdleMUCChannel *self = IDLE_MUC_CHANNEL (base);
	IdleMUCChannelPrivate *priv = self->priv;

	IDLE_DEBUG ("called on %p", self);

	if (priv->state == MUC_STATE_JOINED) {
		tp_message_mixin_set_rescued (G_OBJECT (self));
		tp_base_channel_reopened (base, 0);
	} else {
	/* FIXME: this is wrong if called while JOIN is in flight. */
		tp_base_channel_destroyed (base);
	}
}


static void
idle_muc_channel_destroy (
    TpSvcChannelInterfaceDestroyable *object,
    DBusGMethodInvocation *context)
{
	TpBaseChannel *base = TP_BASE_CHANNEL (object);
	IdleMUCChannel *self = IDLE_MUC_CHANNEL (object);
	IdleMUCChannelPrivate *priv = self->priv;

	IDLE_DEBUG ("called on %p", self);

	if (priv->state == MUC_STATE_JOINED)
		part_from_channel (self, NULL);

	/* FIXME: this is wrong if called while JOIN is in flight. */
	if (priv->state < MUC_STATE_JOINED)
		tp_base_channel_destroyed (base);

	tp_svc_channel_interface_destroyable_return_from_destroy (context);
}


static void idle_muc_channel_get_password_flags (TpSvcChannelInterfacePassword *iface, DBusGMethodInvocation *context) {
	IdleMUCChannel *obj = IDLE_MUC_CHANNEL(iface);
	IdleMUCChannelPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = obj->priv;

	tp_svc_channel_interface_password_return_from_get_password_flags(context, priv->password_flags);
}


static void idle_muc_channel_provide_password (TpSvcChannelInterfacePassword *iface, const gchar * password, DBusGMethodInvocation *context) {
	IdleMUCChannel *obj = IDLE_MUC_CHANNEL(iface);
	IdleMUCChannelPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = obj->priv;

	if (!(priv->password_flags & TP_CHANNEL_PASSWORD_FLAG_PROVIDE) || (priv->passwd_ctx != NULL)) {
		GError *error = g_error_new (TP_ERROR, TP_ERROR_NOT_AVAILABLE,
			"don't need a password now or authentication already "
			"in progress (%s)", priv->channel_name);
		IDLE_DEBUG ("%s", error->message);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return;
	}

	priv->passwd_ctx = context;

	send_join_request(obj, password);
}

/**
 * idle_muc_channel_send
 *
 * Indirectly implements (via TpMessageMixin) D-Bus method Send on interface
 * org.freedesktop.Telepathy.Channel.Type.Text and D-Bus method SendMessage on
 * Channel.Interface.Messages
 */
static void
idle_muc_channel_send (GObject *obj, TpMessage *message, TpMessageSendingFlags flags)
{
	IdleMUCChannel *self = (IdleMUCChannel *) obj;
	IdleMUCChannelPrivate *priv = self->priv;
	TpBaseChannel *base = TP_BASE_CHANNEL (self);
	TpBaseConnection *base_conn = tp_base_channel_get_connection (base);

	if ((priv->mode_state.flags & MODE_FLAG_MODERATED) && !(priv->mode_state.flags & (MODE_FLAG_OPERATOR_PRIVILEGE | MODE_FLAG_HALFOP_PRIVILEGE | MODE_FLAG_VOICE_PRIVILEGE))) {
		GError error = { TP_ERROR, TP_ERROR_PERMISSION_DENIED, "Channel is moderated" };

		IDLE_DEBUG("Channel is moderated");
		tp_message_mixin_sent (obj, message, 0, NULL, &error);
		return;
	}

	idle_text_send(obj, message, flags, priv->channel_name, IDLE_CONNECTION (base_conn));
}

static void
idle_muc_channel_set_subject (
    TpSvcChannelInterfaceSubject *iface,
    const gchar *subject,
    DBusGMethodInvocation *context)
{
  IdleMUCChannel *self = IDLE_MUC_CHANNEL (iface);
  IdleMUCChannelPrivate *priv = self->priv;

  if (priv->state != MUC_STATE_JOINED)
    {
      GError *error = g_error_new (TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Can't set subject: not in the room (state=%s)",
          ascii_muc_states[priv->state]);
      dbus_g_method_return_error (context, error);
      g_clear_error (&error);
    }
  else if (!priv->can_set_topic)
    {
      GError error = { TP_ERROR, TP_ERROR_PERMISSION_DENIED,
          "The channel's +t and you're not an op" };
      dbus_g_method_return_error (context, &error);
    }
  else
    {
      gchar cmd[IRC_MSG_MAXLEN + 2];

      g_snprintf (cmd, IRC_MSG_MAXLEN + 2, "TOPIC %s :%s", priv->channel_name,
          subject);
      send_command (self, cmd);
      /* FIXME: don't return till we get a reply */
      tp_svc_channel_interface_subject_return_from_set_subject (context);
    }
}

gboolean idle_muc_channel_is_modechar(char c) {
	switch (c) {
		/* founder */
		case '*':
		case '~':

		/* admin */
		case '!':
		case '&':

		/* chanop */
		case '@':

		/* halfop */
		case '%':

		/* voice */
		case '+':
			return TRUE;

		default:
			return FALSE;
	}
}

gboolean idle_muc_channel_is_typechar(gchar c)
{
	switch (c) {
		/* standard channel */
		case '#':

		/* local to a server */
		case '&':

		/* no support for modes */
		case '+':

		/* safe channel */
		case '!':
			return TRUE;

		default:
			return FALSE;
	}
}

static void _password_iface_init(gpointer g_iface, gpointer iface_data) {
	TpSvcChannelInterfacePasswordClass *klass = (TpSvcChannelInterfacePasswordClass *)(g_iface);

#define IMPLEMENT(x) tp_svc_channel_interface_password_implement_##x (\
		klass, idle_muc_channel_##x)
	IMPLEMENT(get_password_flags);
	IMPLEMENT(provide_password);
#undef IMPLEMENT
}

static void
subject_iface_init (
    gpointer g_iface,
    gpointer iface_data G_GNUC_UNUSED)
{
  TpSvcChannelInterfaceSubjectClass *klass = g_iface;

#define IMPLEMENT(x) \
  tp_svc_channel_interface_subject_implement_##x (klass, idle_muc_channel_##x)
  IMPLEMENT (set_subject);
#undef IMPLEMENT
}

static void
destroyable_iface_init (
    gpointer g_iface,
    gpointer iface_data G_GNUC_UNUSED)
{
  TpSvcChannelInterfaceDestroyableClass *klass = g_iface;

#define IMPLEMENT(x) \
  tp_svc_channel_interface_destroyable_implement_##x (klass, idle_muc_channel_##x)
  IMPLEMENT (destroy);
#undef IMPLEMENT
}
