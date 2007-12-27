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
#include <telepathy-glib/svc-properties-interface.h>

#define _GNU_SOURCE
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#include "idle-muc-channel.h"

#include "idle-connection.h"
#include "idle-handles.h"
#include "idle-text.h"

#define IDLE_DEBUG_FLAG IDLE_DEBUG_MUC
#include "idle-debug.h"

static void channel_iface_init (gpointer, gpointer);
static void password_iface_init (gpointer, gpointer);
static void properties_iface_init (gpointer, gpointer);
static void text_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(IdleMUCChannel, idle_muc_channel, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL, channel_iface_init);
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP, tp_group_mixin_iface_init);
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL_INTERFACE_PASSWORD, password_iface_init);
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL_TYPE_TEXT, text_iface_init);
		G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_PROPERTIES_INTERFACE, properties_iface_init);
		G_IMPLEMENT_INTERFACE(TP_TYPE_CHANNEL_IFACE, NULL);)

/* signal enum */
enum {
    JOIN_READY,
    LAST_SIGNAL
};

/* property enum */
enum {
	PROP_CONNECTION = 1,
	PROP_OBJECT_PATH,
	PROP_CHANNEL_TYPE,
	PROP_HANDLE_TYPE,
	PROP_HANDLE,
	LAST_PROPERTY_ENUM
};

typedef enum {
	MUC_STATE_CREATED = 0,
	MUC_STATE_JOINING,
	MUC_STATE_NEED_PASSWORD,
	MUC_STATE_JOINED,
	MUC_STATE_PARTED
} IdleMUCState;

#define LAST_MODE_FLAG_SHIFT (15)

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


typedef struct {
	IRCChannelModeFlags flags;
	guint limit;
	gchar *topic;
	gchar *key;
	guint topic_touched;
	guint topic_toucher;
} IRCChannelModeState;

#define TP_TYPE_PROPERTY_INFO_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_STRING, \
      G_TYPE_UINT, \
      G_TYPE_INVALID))
#define TP_TYPE_PROPERTY_INFO_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_PROPERTY_INFO_STRUCT))

#define TP_TYPE_PROPERTY_VALUE_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_VALUE, \
      G_TYPE_INVALID))
#define TP_TYPE_PROPERTY_VALUE_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_PROPERTY_VALUE_STRUCT))

#define TP_TYPE_PROPERTY_FLAGS_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_INVALID))
#define TP_TYPE_PROPERTY_FLAGS_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_PROPERTY_FLAGS_STRUCT))

typedef enum {
	TP_PROPERTY_INVITE_ONLY = 0,
	TP_PROPERTY_LIMIT,
	TP_PROPERTY_LIMITED,
	TP_PROPERTY_MODERATED,
	TP_PROPERTY_PASSWORD,
	TP_PROPERTY_PASSWORD_REQUIRED,
	TP_PROPERTY_PRIVATE,
	TP_PROPERTY_SUBJECT,
	TP_PROPERTY_SUBJECT_TIMESTAMP,
	TP_PROPERTY_SUBJECT_CONTACT,
	LAST_TP_PROPERTY_ENUM
} IdleMUCChannelTPProperty;

typedef struct {
	const gchar *name;
	GType type;
} TPPropertySignature;

typedef struct {
	GValue *value;
	guint flags;
} TPProperty;


static const TPPropertySignature property_signatures[] = {
	{"invite-only", G_TYPE_BOOLEAN},
	{"limit", G_TYPE_UINT},
	{"limited", G_TYPE_BOOLEAN},
	{"moderated", G_TYPE_BOOLEAN},
	{"password", G_TYPE_STRING},
	{"password-required", G_TYPE_BOOLEAN},
	{"private", G_TYPE_BOOLEAN},
	{"subject", G_TYPE_STRING},
	{"subject-timestamp", G_TYPE_UINT},
	{"subject-contact", G_TYPE_UINT},
	{NULL, G_TYPE_NONE}
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

static void muc_channel_tp_properties_init(IdleMUCChannel *chan);
static void muc_channel_tp_properties_destroy(IdleMUCChannel *chan);
static void change_tp_properties(IdleMUCChannel *chan, const GPtrArray *props);
static void set_tp_property_flags(IdleMUCChannel *chan, const GArray *prop_ids, TpPropertyFlags add, TpPropertyFlags remove);

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _IdleMUCChannelPrivate IdleMUCChannelPrivate;

struct _IdleMUCChannelPrivate {
	IdleConnection *connection;
	gchar *object_path;
	TpHandle handle;
	const gchar *channel_name;

	IdleMUCState state;

	IRCChannelModeState mode_state;

	guint password_flags;
	TPProperty *properties;

	DBusGMethodInvocation *passwd_ctx;

	/* NAMEREPLY MembersChanged aggregation */
	TpHandleSet *namereply_set;

	gboolean join_ready;
	gboolean closed;

	gboolean dispose_has_run;
};

static void change_password_flags(IdleMUCChannel *chan, guint flag, gboolean state);

#define IDLE_MUC_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), IDLE_TYPE_MUC_CHANNEL, IdleMUCChannelPrivate))

static void
idle_muc_channel_init (IdleMUCChannel *obj) {
  IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE (obj);

  priv->password_flags = 0;

  priv->closed = FALSE;
  priv->state = MUC_STATE_CREATED;

  priv->mode_state.flags = 0;
  priv->mode_state.topic = NULL;
  priv->mode_state.key = NULL;

  priv->properties = g_new0(TPProperty, LAST_TP_PROPERTY_ENUM);
  muc_channel_tp_properties_init(obj);

  priv->dispose_has_run = FALSE;
}

static void idle_muc_channel_dispose (GObject *object);
static void idle_muc_channel_finalize (GObject *object);

static GObject *idle_muc_channel_constructor(GType type, guint n_props, GObjectConstructParam *props) {
	GObject *obj = G_OBJECT_CLASS(idle_muc_channel_parent_class)->constructor(type, n_props, props);
	IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);
	DBusGConnection *bus;
	TpHandleRepoIface *room_handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_ROOM);
	TpHandleRepoIface *contact_handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT);

	tp_handle_ref(room_handles, priv->handle);
	g_assert(tp_handle_is_valid(room_handles, priv->handle, NULL));
	priv->channel_name = tp_handle_inspect(room_handles, priv->handle);

	bus = tp_get_bus();
	dbus_g_connection_register_g_object(bus, priv->object_path, obj);

	tp_group_mixin_init(obj, G_STRUCT_OFFSET(IdleMUCChannel, group), contact_handles, priv->connection->parent.self_handle);

	tp_text_mixin_init(obj, G_STRUCT_OFFSET(IdleMUCChannel, text), contact_handles);
  tp_text_mixin_set_message_types(obj,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
      G_MAXUINT);

	return obj;
}

static void idle_muc_channel_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
	IdleMUCChannel *chan;
	IdleMUCChannelPrivate *priv;

	g_assert(object != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(object));

	chan = IDLE_MUC_CHANNEL(object);
	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);

	switch (property_id) {
		case PROP_CONNECTION:
			g_value_set_object(value, priv->connection);
			break;

		case PROP_OBJECT_PATH:
			g_value_set_string(value, priv->object_path);
			break;

		case PROP_CHANNEL_TYPE:
			g_value_set_string(value, TP_IFACE_CHANNEL_TYPE_TEXT);
			break;

		case PROP_HANDLE_TYPE:
			g_value_set_uint(value, TP_HANDLE_TYPE_ROOM);
			break;

		case PROP_HANDLE:
			g_value_set_uint(value, priv->handle);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void idle_muc_channel_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
	IdleMUCChannel *chan = IDLE_MUC_CHANNEL(object);
	IdleMUCChannelPrivate *priv;

	g_assert(chan != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(chan));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);

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
			IDLE_DEBUG("setting handle to %u", priv->handle);
			break;

		case PROP_HANDLE_TYPE:
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void
idle_muc_channel_class_init (IdleMUCChannelClass *idle_muc_channel_class) {
  GObjectClass *object_class = G_OBJECT_CLASS (idle_muc_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (idle_muc_channel_class, sizeof (IdleMUCChannelPrivate));

  object_class->constructor = idle_muc_channel_constructor;

  object_class->get_property = idle_muc_channel_get_property;
  object_class->set_property = idle_muc_channel_set_property;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH, "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE, "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE, "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  object_class->dispose = idle_muc_channel_dispose;
  object_class->finalize = idle_muc_channel_finalize;

  param_spec = g_param_spec_object ("connection", "IdleConnection object", "The IdleConnection object that owns this MUCChannel object.", IDLE_TYPE_CONNECTION, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  signals[JOIN_READY] = g_signal_new("join-ready", G_OBJECT_CLASS_TYPE(idle_muc_channel_class), G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);

	tp_group_mixin_class_init(object_class, G_STRUCT_OFFSET(IdleMUCChannelClass, group_class), add_member, remove_member);
	tp_text_mixin_class_init(object_class, G_STRUCT_OFFSET(IdleMUCChannelClass, text_class));
}

void
idle_muc_channel_dispose (GObject *object) {
  IdleMUCChannel *self = IDLE_MUC_CHANNEL (object);
  IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (!priv->closed)
	  tp_svc_channel_emit_closed((TpSvcChannel *)(self));

  if (G_OBJECT_CLASS (idle_muc_channel_parent_class)->dispose)
    G_OBJECT_CLASS (idle_muc_channel_parent_class)->dispose (object);
}

void
idle_muc_channel_finalize (GObject *object) {
  IdleMUCChannel *self = IDLE_MUC_CHANNEL (object);
  IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_ROOM);
  tp_handle_unref(handles, priv->handle);

  if (priv->object_path)
	  g_free(priv->object_path);

  if (priv->mode_state.topic)
	  g_free(priv->mode_state.topic);

  if (priv->mode_state.key)
	  g_free(priv->mode_state.key);

  muc_channel_tp_properties_destroy(self);
  g_free(priv->properties);

	if (priv->namereply_set)
		tp_handle_set_destroy(priv->namereply_set);

	tp_text_mixin_finalize(object);
	tp_group_mixin_finalize(object);

  G_OBJECT_CLASS (idle_muc_channel_parent_class)->finalize (object);
}

static void muc_channel_tp_properties_init(IdleMUCChannel *chan) {
	IdleMUCChannelPrivate *priv;
	TPProperty *props;
	int i;

	g_assert(chan != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(chan));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);
	props = priv->properties;

	for (i=0; i<LAST_TP_PROPERTY_ENUM; i++) {
		GValue *value;
		props[i].value = value = g_new0(GValue, 1);

		g_value_init(value, property_signatures[i].type);

		props[i].flags = 0;
	}
}

static void muc_channel_tp_properties_destroy(IdleMUCChannel *chan) {
	IdleMUCChannelPrivate *priv;
	TPProperty *props;
	int i;

	g_assert(chan != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(chan));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);
	props = priv->properties;

	for (i=0; i<LAST_TP_PROPERTY_ENUM; i++) {
		g_value_unset(props[i].value);
		g_free(props[i].value);
	}
}

static gboolean g_value_compare(const GValue *v1, const GValue *v2) {
	GType t1, t2;

	g_assert(v1 != NULL);
	g_assert(v2 != NULL);

	g_assert(G_IS_VALUE(v1));
	g_assert(G_IS_VALUE(v2));

	t1 = G_VALUE_TYPE(v1);
	t2 = G_VALUE_TYPE(v2);

	if (t1 != t2) {
		IDLE_DEBUG("different types %s and %s compared!", g_type_name(t1), g_type_name(t2));
		return FALSE;
	}

	switch (t1) {
		case G_TYPE_BOOLEAN:
			return g_value_get_boolean(v1) == g_value_get_boolean(v2);

		case G_TYPE_UINT:
			return g_value_get_uint(v1) == g_value_get_uint(v2);

		case G_TYPE_STRING: {
			const gchar *s1, *s2;

			s1 = g_value_get_string(v1);
			s2 = g_value_get_string(v2);

			if ((s1 == NULL) && (s2 == NULL))
				return TRUE;
			else if ((s1 == NULL) || (s2 == NULL))
				return FALSE;
			else
				return (strcmp(s1, s2) == 0);
		}

		default:
			IDLE_DEBUG("unknown type %s in comparison", g_type_name(t1));
			return FALSE;
	}
}

static void change_tp_properties(IdleMUCChannel *chan, const GPtrArray *props) {
	IdleMUCChannelPrivate *priv;
	int i;
	GPtrArray *changed_props;
	GArray *flags;

	g_assert(chan != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(chan));
	g_assert(props != NULL);

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);

	changed_props = g_ptr_array_new();
	flags = g_array_new(FALSE, FALSE, sizeof(guint));

	for (i = 0; i < props->len; i++) {
		GValue *curr_val;
		GValue prop = {0, };
		GValue *new_val;
		guint prop_id;

		g_value_init(&prop, TP_TYPE_PROPERTY_VALUE_STRUCT);
		g_value_set_static_boxed(&prop, g_ptr_array_index(props, i));

		dbus_g_type_struct_get(&prop,
								0, &prop_id,
								1, &new_val,
								G_MAXUINT);

		if (prop_id >= LAST_TP_PROPERTY_ENUM) {
			IDLE_DEBUG("prop_id >= LAST_TP_PROPERTY_ENUM, corruption!11");
			continue;
		}

		curr_val = priv->properties[prop_id].value;

		if (!g_value_compare(new_val, curr_val)) {
			g_value_copy(new_val, curr_val);

			g_ptr_array_add(changed_props, g_value_get_boxed(&prop));
			g_array_append_val(flags, prop_id);

			IDLE_DEBUG("tp_property %u changed", prop_id);
		}

		g_value_unset(&prop);
	}

	if (changed_props->len > 0) {
		IDLE_DEBUG("emitting PROPERTIES_CHANGED with %u properties", changed_props->len);
		tp_svc_properties_interface_emit_properties_changed((TpSvcPropertiesInterface *)(chan), changed_props);
	}

	if (flags->len > 0) {
		IDLE_DEBUG("flagging properties as readable with %u props", flags->len);
		set_tp_property_flags(chan, flags, TP_PROPERTY_FLAG_READ, 0);
	}

	g_ptr_array_free(changed_props, TRUE);
	g_array_free(flags, TRUE);
}

static void set_tp_property_flags(IdleMUCChannel *chan, const GArray *props, TpPropertyFlags add, TpPropertyFlags remove) {
	IdleMUCChannelPrivate *priv;
	int i;
	GPtrArray *changed_props;

	g_assert(chan != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(chan));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);

	changed_props = g_ptr_array_new();

	if (props == NULL) {
		IDLE_DEBUG("setting all flags with %u, %u", add, remove);

		for (i=0; i<LAST_TP_PROPERTY_ENUM; i++) {
			guint curr_flags = priv->properties[i].flags;
			guint flags = (curr_flags | add) & (~remove);

			if (curr_flags != flags) {
				GValue prop = {0, };

				g_value_init(&prop, TP_TYPE_PROPERTY_FLAGS_STRUCT);
				g_value_take_boxed(&prop, dbus_g_type_specialized_construct(TP_TYPE_PROPERTY_FLAGS_STRUCT));

				dbus_g_type_struct_set(&prop,
										0, i,
										1, flags,
										G_MAXUINT);

				priv->properties[i].flags = flags;

				g_ptr_array_add(changed_props, g_value_get_boxed(&prop));
			}
		}
	}	else {
		for (i=0; i<props->len; i++) {
			guint prop_id = g_array_index(props, guint, i);
			guint curr_flags = priv->properties[prop_id].flags;
			guint flags = (curr_flags | add) & (~remove);

			if (curr_flags != flags) {
				GValue prop = {0, };

				g_value_init(&prop, TP_TYPE_PROPERTY_FLAGS_STRUCT);
				g_value_take_boxed(&prop, dbus_g_type_specialized_construct(TP_TYPE_PROPERTY_FLAGS_STRUCT));

				dbus_g_type_struct_set(&prop,
										0, prop_id,
										1, flags,
										G_MAXUINT);

				priv->properties[prop_id].flags = flags;

				g_ptr_array_add(changed_props, g_value_get_boxed(&prop));
			}
		}
	}

	if (changed_props->len > 0) {
		IDLE_DEBUG("emitting PROPERTY_FLAGS_CHANGED with %u properties", changed_props->len);
		tp_svc_properties_interface_emit_property_flags_changed((TpSvcPropertiesInterface *)(chan), changed_props);
	}

	g_ptr_array_free(changed_props, TRUE);
}

static void provide_password_reply(IdleMUCChannel *chan, gboolean success) {
	IdleMUCChannelPrivate *priv;

	g_assert(chan != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(chan));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);

	if (priv->passwd_ctx != NULL) {
		tp_svc_channel_interface_password_return_from_provide_password(priv->passwd_ctx, success);
		priv->passwd_ctx = NULL;
	}	else {
		IDLE_DEBUG("don't have a ProvidePassword context to return with! (channel handle %u)", priv->handle);
	}

	if (success) {
		change_password_flags(chan, TP_CHANNEL_PASSWORD_FLAG_PROVIDE, 0);
	}
}

static void change_state(IdleMUCChannel *obj, IdleMUCState state) {
	IdleMUCChannelPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	if ((state > MUC_STATE_JOINING) && (!priv->join_ready)) {
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

	IDLE_DEBUG("IdleMUCChannel %u changed to state %s", priv->handle, ascii_muc_states[state]);
}

static IdleMUCChannelTPProperty to_prop_id(IRCChannelModeFlags flag) {
	switch (flag) {
		case MODE_FLAG_INVITE_ONLY:
			return TP_PROPERTY_INVITE_ONLY;

		case MODE_FLAG_MODERATED:
			return TP_PROPERTY_MODERATED;

		case MODE_FLAG_PRIVATE:
		case MODE_FLAG_SECRET:
			return TP_PROPERTY_PRIVATE;

		case MODE_FLAG_KEY:
			return TP_PROPERTY_PASSWORD_REQUIRED;

		case MODE_FLAG_USER_LIMIT:
			return TP_PROPERTY_LIMITED;

		default:
			return LAST_TP_PROPERTY_ENUM;
	}
}

static void change_mode_state(IdleMUCChannel *obj, guint add, guint remove) {
	IdleMUCChannelPrivate *priv;
	IRCChannelModeFlags flags;
	guint group_add = 0, group_remove = 0;
	GPtrArray *tp_props_to_change;
	guint prop_flags = 0;
	guint combined;
	int i;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	remove &= ~add;

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);
	flags = priv->mode_state.flags;

	tp_props_to_change = g_ptr_array_new();

	IDLE_DEBUG("got %x, %x", add, remove);

	add &= ~flags;
	remove &= flags;

	IDLE_DEBUG("operation %x, %x", add, remove);

	flags |= add;
	flags &= ~remove;

	combined = add|remove;

	if (add & MODE_FLAG_INVITE_ONLY) {
		if (!(flags & (MODE_FLAG_OPERATOR_PRIVILEGE|MODE_FLAG_HALFOP_PRIVILEGE)))
			group_remove |= TP_CHANNEL_GROUP_FLAG_CAN_ADD;
	}	else if (remove & MODE_FLAG_INVITE_ONLY) {
		group_add |= TP_CHANNEL_GROUP_FLAG_CAN_ADD;
	}

	if (combined & (MODE_FLAG_OPERATOR_PRIVILEGE | MODE_FLAG_HALFOP_PRIVILEGE)) {
		GArray *flags_to_change;

		static const guint flags_helper[] = {
			TP_PROPERTY_INVITE_ONLY,
			TP_PROPERTY_LIMIT,
			TP_PROPERTY_LIMITED,
			TP_PROPERTY_MODERATED,
			TP_PROPERTY_PASSWORD,
			TP_PROPERTY_PASSWORD_REQUIRED,
			TP_PROPERTY_PRIVATE,
			TP_PROPERTY_SUBJECT,
			LAST_TP_PROPERTY_ENUM
		};

		flags_to_change = g_array_new(FALSE, FALSE, sizeof(guint));

		for (i=0; flags_helper[i] != LAST_TP_PROPERTY_ENUM; i++) {
			guint prop_id = flags_helper[i];
			g_array_append_val(flags_to_change, prop_id);
		}

		prop_flags = TP_PROPERTY_FLAG_WRITE;

		if (add & (MODE_FLAG_OPERATOR_PRIVILEGE|MODE_FLAG_HALFOP_PRIVILEGE)) {
			group_add |= TP_CHANNEL_GROUP_FLAG_CAN_ADD|TP_CHANNEL_GROUP_FLAG_CAN_REMOVE|TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE;

			set_tp_property_flags(obj, flags_to_change, prop_flags, 0);
		} else if (remove & (MODE_FLAG_OPERATOR_PRIVILEGE|MODE_FLAG_HALFOP_PRIVILEGE)) {
			group_remove |= TP_CHANNEL_GROUP_FLAG_CAN_REMOVE|TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE;

			if (flags & MODE_FLAG_INVITE_ONLY)
				group_remove |= TP_CHANNEL_GROUP_FLAG_CAN_ADD;

			set_tp_property_flags(obj, flags_to_change, 0, prop_flags);
		}
	}

	for (i = 1; i<LAST_MODE_FLAG_ENUM; i = (i << 1)) {
		if (combined & i) {
			IdleMUCChannelTPProperty tp_prop_id;

			tp_prop_id = to_prop_id(i);

			if (tp_prop_id < LAST_TP_PROPERTY_ENUM) {
				GValue prop = {0, };
				GValue val_auto_is_fine = {0, };
				GValue *val = &val_auto_is_fine;
				GType type = property_signatures[tp_prop_id].type;

				g_value_init(&prop, TP_TYPE_PROPERTY_VALUE_STRUCT);
				g_value_take_boxed(&prop, dbus_g_type_specialized_construct(TP_TYPE_PROPERTY_VALUE_STRUCT));

				g_value_init(val, type);

				if (type != G_TYPE_BOOLEAN) {
					IDLE_DEBUG("type != G_TYPE_BOOLEAN for %u (modeflag %u), ignoring", tp_prop_id, i);
					continue;
				}

				g_value_set_boolean(val, (add & i) ? TRUE : FALSE);

				dbus_g_type_struct_set(&prop,
						0, tp_prop_id,
						1, val,
						G_MAXUINT);

				g_ptr_array_add(tp_props_to_change, g_value_get_boxed(&prop));

				if (add & i) {
					GValue prop = {0, };
					GValue val_auto_is_fine = {0, };
					GValue *val = &val_auto_is_fine;

					g_value_init(&prop, TP_TYPE_PROPERTY_VALUE_STRUCT);
					g_value_take_boxed(&prop, dbus_g_type_specialized_construct(TP_TYPE_PROPERTY_VALUE_STRUCT));

					if (i == MODE_FLAG_USER_LIMIT) {
						g_value_init(val, G_TYPE_UINT);
						g_value_set_uint(val, priv->mode_state.limit);
						tp_prop_id = TP_PROPERTY_LIMIT;
					} else if (i == MODE_FLAG_KEY) {
						g_value_init(val, G_TYPE_STRING);
						g_value_set_string(val, priv->mode_state.key);
						tp_prop_id = TP_PROPERTY_PASSWORD;
					}	else {
						continue;
					}

					dbus_g_type_struct_set(&prop,
											0, tp_prop_id,
											1, val,
											G_MAXUINT);

					g_ptr_array_add(tp_props_to_change, g_value_get_boxed(&prop));
				}
			}
		}
	}

	tp_group_mixin_change_flags((GObject *)obj, group_add, group_remove);
	change_tp_properties(obj, tp_props_to_change);

	priv->mode_state.flags = flags;

	IDLE_DEBUG("changed to %x", flags);
}

static void change_password_flags(IdleMUCChannel *obj, guint flag, gboolean state) {
	IdleMUCChannelPrivate *priv;
	guint add = 0, remove = 0;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	if (state) {
		add = (~(priv->password_flags)) & flag;
		priv->password_flags |= flag;
	}	else {
		remove = priv->password_flags & flag;
		priv->password_flags &= ~flag;
	}

	if (add | remove) {
		IDLE_DEBUG("emitting PASSWORD_FLAGS_CHANGED with %u %u", add, remove);
		tp_svc_channel_interface_password_emit_password_flags_changed((TpSvcChannelInterfacePassword *)(obj), add, remove);
	}
}

gboolean _idle_muc_channel_receive(IdleMUCChannel *chan, TpChannelTextMessageType type, TpHandle sender, const gchar *text) {
	time_t stamp = time(NULL);

	g_assert(chan != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(chan));

	return tp_text_mixin_receive(G_OBJECT(chan), type, sender, stamp, text);
}

static void send_mode_query_request(IdleMUCChannel *chan) {
	IdleMUCChannelPrivate *priv;
	gchar cmd[IRC_MSG_MAXLEN+2];

	g_assert(chan != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(chan));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);

	g_snprintf(cmd, IRC_MSG_MAXLEN+2, "MODE %s", priv->channel_name);

	_idle_connection_send(priv->connection, cmd);
}

void _idle_muc_channel_join(IdleMUCChannel *chan, TpHandle joiner) {
	IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);
	TpIntSet *set;

	set = tp_intset_new();
	tp_intset_add(set, joiner);

	if (joiner == priv->connection->parent.self_handle) {
		/* woot we managed to get into a channel, great */
		change_state(chan, MUC_STATE_JOINED);
		tp_group_mixin_change_members((GObject *)(chan), NULL, set, NULL, NULL, NULL, joiner, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
		tp_group_mixin_change_flags((GObject *)(chan), TP_CHANNEL_GROUP_FLAG_CAN_ADD, 0);

		send_mode_query_request(chan);

		if (priv->channel_name[0] == '+')
			/* according to IRC specs, PLUS channels do not support channel modes and alway have only +t set, so we work with that. */
			change_mode_state(chan, MODE_FLAG_TOPIC_ONLY_SETTABLE_BY_OPS, 0);
	}	else {
		tp_group_mixin_change_members((GObject *)(chan), NULL, set, NULL, NULL, NULL, joiner, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
	}

	IDLE_DEBUG("member joined with handle %u", joiner);

	tp_intset_destroy(set);
}

static void _network_member_left(IdleMUCChannel *chan, TpHandle leaver, TpHandle actor, const gchar *message, TpChannelGroupChangeReason reason) {
	IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);
	TpIntSet *set = tp_intset_new();

	tp_intset_add(set, leaver);
	tp_group_mixin_change_members((GObject *) chan, message, NULL, set, NULL, NULL, actor, reason);

	if (leaver == priv->connection->parent.self_handle) {
		change_state(chan, MUC_STATE_PARTED);

		if (!priv->closed) {
			priv->closed = TRUE;
			tp_svc_channel_emit_closed((TpSvcChannel *) chan);
		}
	}

	tp_intset_destroy(set);
}

void _idle_muc_channel_part(IdleMUCChannel *chan, TpHandle leaver, const gchar *message) {
	return _network_member_left(chan, leaver, leaver, message, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
}

void _idle_muc_channel_kick(IdleMUCChannel *chan, TpHandle kicked, TpHandle kicker, const gchar *message) {
	return _network_member_left(chan, kicked, kicker, message, TP_CHANNEL_GROUP_CHANGE_REASON_KICKED);
}

void _idle_muc_channel_quit(IdleMUCChannel *chan, TpHandle quitter, const gchar *message) {
	return _network_member_left(chan, quitter, quitter, message, TP_CHANNEL_GROUP_CHANGE_REASON_OFFLINE);
}

void _idle_muc_channel_invited(IdleMUCChannel *chan, TpHandle inviter) {
	IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);
	TpIntSet *add = tp_intset_new();
	TpIntSet *local = tp_intset_new();

	tp_intset_add(add, inviter);
	tp_intset_add(local, priv->connection->parent.self_handle);

	tp_group_mixin_change_members((GObject *)(chan), NULL, add, NULL, local, NULL, inviter, TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);

	tp_intset_destroy(add);
	tp_intset_destroy(local);
}

void _idle_muc_channel_namereply(IdleMUCChannel *chan, GValueArray *args) {
	IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);

	if (!priv->namereply_set)
		priv->namereply_set = tp_handle_set_new(tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT));

	for (int i = 1; (i + 1) < args->n_values; i += 2) {
		TpHandle handle = g_value_get_uint(g_value_array_get_nth(args, i));
		gchar modechar = g_value_get_char(g_value_array_get_nth(args, i + 1));

		if (handle == priv->connection->parent.self_handle) {
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

void _idle_muc_channel_namereply_end(IdleMUCChannel *chan) {
	IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);

	if (!priv->namereply_set) {
		IDLE_DEBUG("no NAMEREPLY received before NAMEREPLY_END");
		return;
	}

	idle_connection_emit_queued_aliases_changed(priv->connection);

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

void _idle_muc_channel_mode(IdleMUCChannel *chan, GValueArray *args) {
	IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);
	TpHandleRepoIface *handles = tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT);
	GArray *flags_to_change = g_array_new(FALSE, FALSE, sizeof(guint));
	static const guint flags_helper[] = {
		TP_PROPERTY_INVITE_ONLY,
		TP_PROPERTY_LIMITED,
		TP_PROPERTY_MODERATED,
		TP_PROPERTY_PASSWORD_REQUIRED,
		TP_PROPERTY_PRIVATE,
		LAST_TP_PROPERTY_ENUM
	};

	for (const guint *prop_id = flags_helper; *prop_id != LAST_TP_PROPERTY_ENUM; prop_id++)
		g_array_append_val(flags_to_change, *prop_id);

	set_tp_property_flags(chan, flags_to_change, TP_PROPERTY_FLAG_READ, 0);

	g_array_free(flags_to_change, TRUE);

	for (int i = 1; i < args->n_values; i++) {
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

						if (handle == priv->connection->parent.self_handle) {
							IDLE_DEBUG("got MODE '%c' concerning us", *modes);
							mode_accum |= _modechar_to_modeflag(*modes);
						}

						if (handle)
							tp_handle_unref(handles, handle);
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

void _idle_muc_channel_topic(IdleMUCChannel *chan, const char *topic) {
	GValue prop = {0, };
	GValue val = {0, };
	GPtrArray *arr;
	IdleMUCChannelPrivate *priv;

	g_assert(chan != NULL);
	g_assert(topic != NULL);

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);

	g_value_init(&prop, TP_TYPE_PROPERTY_VALUE_STRUCT);
	g_value_take_boxed(&prop, dbus_g_type_specialized_construct(TP_TYPE_PROPERTY_VALUE_STRUCT));

	g_value_init(&val, G_TYPE_STRING);
	g_value_set_string(&val, topic);

	dbus_g_type_struct_set(&prop,
			0, TP_PROPERTY_SUBJECT,
			1, &val,
			G_MAXUINT);

	arr = g_ptr_array_new();
	g_ptr_array_add(arr, g_value_get_boxed(&prop));
	change_tp_properties(chan, arr);
	g_ptr_array_free(arr, TRUE);
}

void _idle_muc_channel_topic_touch(IdleMUCChannel *chan, const TpHandle toucher, const guint timestamp) {
	GValue prop = {0, };
	GValue val = {0, };
	GPtrArray *arr;

	arr = g_ptr_array_new();

	g_assert(chan != NULL);
	g_assert(toucher != 0);

	g_value_init(&prop, TP_TYPE_PROPERTY_VALUE_STRUCT);
	g_value_take_boxed(&prop, dbus_g_type_specialized_construct(TP_TYPE_PROPERTY_VALUE_STRUCT));

	g_value_init(&val, G_TYPE_UINT);
	g_value_set_uint(&val, toucher);

	dbus_g_type_struct_set(&prop,
							0, TP_PROPERTY_SUBJECT_CONTACT,
							1, &val,
							G_MAXUINT);

	g_ptr_array_add(arr, g_value_get_boxed(&prop));

	g_value_set_uint(&val, timestamp);

	dbus_g_type_struct_set(&prop,
							0, TP_PROPERTY_SUBJECT_TIMESTAMP,
							1, &val,
							G_MAXUINT);

	g_ptr_array_add(arr, g_value_get_boxed(&prop));

	change_tp_properties(chan, arr);

	g_ptr_array_free(arr, TRUE);
}

void _idle_muc_channel_topic_full(IdleMUCChannel *chan, const TpHandle toucher, const guint timestamp, const gchar *topic) {
	GValue prop = {0, };
	GValue val = {0, };
	GPtrArray *arr;

	arr = g_ptr_array_new();

	g_assert(chan != NULL);
	g_assert(toucher != 0);

	g_value_init(&prop, TP_TYPE_PROPERTY_VALUE_STRUCT);
	g_value_take_boxed(&prop, dbus_g_type_specialized_construct(TP_TYPE_PROPERTY_VALUE_STRUCT));

	g_value_init(&val, G_TYPE_UINT);
	g_value_set_uint(&val, toucher);
	dbus_g_type_struct_set(&prop,
			0, TP_PROPERTY_SUBJECT_CONTACT,
			1, &val,
			G_MAXUINT);
	g_ptr_array_add(arr, g_value_get_boxed(&prop));

	g_value_set_uint(&val, timestamp);
	dbus_g_type_struct_set(&prop,
			0, TP_PROPERTY_SUBJECT_TIMESTAMP,
			1, &val,
			G_MAXUINT);
	g_ptr_array_add(arr, g_value_get_boxed(&prop));

	g_value_unset(&val);
	g_value_init(&val, G_TYPE_STRING);
	g_value_set_string(&val, topic);
	dbus_g_type_struct_set(&prop,
							0, TP_PROPERTY_SUBJECT,
							1, &val,
							G_MAXUINT);
	change_tp_properties(chan, arr);

	g_ptr_array_free(arr, TRUE);
}

void _idle_muc_channel_topic_unset(IdleMUCChannel *chan) {
	GArray *arr = g_array_new(FALSE, FALSE, sizeof(guint));
	guint not_even_g_array_append_can_take_address_of_enumeration_constants_in_c;
	guint *tmp = &not_even_g_array_append_can_take_address_of_enumeration_constants_in_c;

	*tmp = TP_PROPERTY_SUBJECT;
	g_array_append_val(arr, *tmp);
	*tmp = TP_PROPERTY_SUBJECT_TIMESTAMP;
	g_array_append_val(arr, *tmp);
	*tmp = TP_PROPERTY_SUBJECT_CONTACT;
	g_array_append_val(arr, *tmp);

	set_tp_property_flags(chan, arr, 0, TP_PROPERTY_FLAG_READ);

	g_array_free(arr, TRUE);
}

void _idle_muc_channel_badchannelkey(IdleMUCChannel *chan) {
	change_state(chan, MUC_STATE_NEED_PASSWORD);
}

void _idle_muc_channel_join_error(IdleMUCChannel *chan, IdleMUCChannelJoinError err) {
	IdleMUCChannelPrivate *priv;

	g_assert(chan != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(chan));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(chan);

	if (!priv->join_ready) {
		priv->join_ready = TRUE;

		g_signal_emit(chan, signals[JOIN_READY], 0, err);
	}	else {
		IDLE_DEBUG("already emitted JOIN_READY! (current err %u)", err);
	}
}

void _idle_muc_channel_rename(IdleMUCChannel *chan, TpHandle old_handle, TpHandle new_handle) {
	TpIntSet *add = tp_intset_new();
	TpIntSet *remove = tp_intset_new();
	TpIntSet *local = tp_intset_new();
	TpIntSet *remote = tp_intset_new();

	/* FIXME when self renaming lands in TpGroupMixin, use that */
	if (old_handle == chan->group.self_handle)
		chan->group.self_handle = new_handle;

	tp_intset_add(remove, old_handle);

	if (tp_handle_set_is_member(chan->group.members, old_handle))
		tp_intset_add(add, new_handle);
	else if (tp_handle_set_is_member(chan->group.local_pending, old_handle))
		tp_intset_add(local, new_handle);
	else if (tp_handle_set_is_member(chan->group.remote_pending, old_handle))
		tp_intset_add(remote, new_handle);
	else
		goto cleanup;

	/* FIXME use REASON_RENAMED when it gets in telepathy-glib */
	tp_group_mixin_change_members((GObject *) chan, NULL, add, remove, local, remote, new_handle, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

cleanup:

	tp_intset_destroy(add);
	tp_intset_destroy(remove);
	tp_intset_destroy(local);
	tp_intset_destroy(remote);
}

static void send_join_request(IdleMUCChannel *obj, const gchar *password) {
	IdleMUCChannelPrivate *priv;
	gchar cmd[IRC_MSG_MAXLEN+1];

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	if (password)
		g_snprintf(cmd, IRC_MSG_MAXLEN+1, "JOIN %s %s", priv->channel_name, password);
	else
		g_snprintf(cmd, IRC_MSG_MAXLEN+1, "JOIN %s", priv->channel_name);

	_idle_connection_send(priv->connection, cmd);
}

void _idle_muc_channel_join_attempt(IdleMUCChannel *obj) {
	return send_join_request(obj, NULL);
}

static gboolean send_invite_request(IdleMUCChannel *obj, TpHandle handle, GError **error) {
	IdleMUCChannelPrivate *priv;
	gchar cmd[IRC_MSG_MAXLEN+1];
	const gchar *nick;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	nick = tp_handle_inspect(tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT), handle);

	if ((nick == NULL) || (nick[0] == '\0')) {
		IDLE_DEBUG("invalid handle %u passed", handle);

		g_set_error(error, TP_ERRORS, TP_ERROR_INVALID_HANDLE, "invalid handle %u passed", handle);

		return FALSE;
	}

	g_snprintf(cmd, IRC_MSG_MAXLEN+1, "INVITE %s %s", nick, priv->channel_name);

	_idle_connection_send(priv->connection, cmd);

	return TRUE;
}

static gboolean send_kick_request(IdleMUCChannel *obj, TpHandle handle, const gchar *msg, GError **error) {
	IdleMUCChannelPrivate *priv;
	gchar cmd[IRC_MSG_MAXLEN+1];
	const gchar *nick;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	nick = tp_handle_inspect(tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT), handle);

	if ((nick == NULL) || (nick[0] == '\0')) {
		IDLE_DEBUG("invalid handle %u passed", handle);

		*error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_HANDLE, "invalid handle %u passed", handle);

		return FALSE;
	}

	if (msg != NULL) {
		g_snprintf(cmd, IRC_MSG_MAXLEN+1, "KICK %s %s %s", priv->channel_name, nick, msg);
	}	else {
		g_snprintf(cmd, IRC_MSG_MAXLEN+1, "KICK %s %s", priv->channel_name, nick);
	}

	_idle_connection_send(priv->connection, cmd);

	return TRUE;
}

static gboolean add_member(GObject *gobj, TpHandle handle, const gchar *message, GError **error) {
	IdleMUCChannel *obj = IDLE_MUC_CHANNEL(gobj);
	IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	if (handle == priv->connection->parent.self_handle) {
		if (tp_handle_set_is_member(obj->group.members, handle) || tp_handle_set_is_member(obj->group.remote_pending, handle)) {
			IDLE_DEBUG("we are already a member of or trying to join the channel with handle %u", priv->handle);

			*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "we are already a member of or trying to join the channel with handle %u", priv->handle);

			return FALSE;
		}	else {
			TpIntSet *add_set = tp_intset_new();

			send_join_request(obj, NULL);

			change_state(obj, MUC_STATE_JOINING);

			tp_intset_add(add_set, handle);

			tp_group_mixin_change_members(gobj, message, NULL, NULL, NULL, add_set, handle, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
		}
	}	else {
		if (tp_handle_set_is_member(obj->group.members, handle) || tp_handle_set_is_member(obj->group.remote_pending, handle)) {
			IDLE_DEBUG("the requested contact (handle %u) to be added to the room (handle %u) is already a member of or has already been invited to join the room", handle, priv->handle);

			*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "the requested contact (handle %u) to be added to the room (handle %u) is already a member of or has already been invited to join the room", handle, priv->handle);

			return FALSE;
		}	else {
			GError *invite_error;
			TpIntSet *add_set = tp_intset_new();

			if (!send_invite_request(obj, handle, &invite_error)) {
				*error = invite_error;

				return FALSE;
			}

			tp_intset_add(add_set, handle);

			tp_group_mixin_change_members(gobj, NULL, NULL, NULL, NULL, add_set, priv->connection->parent.self_handle, TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);
		}
	}

	return TRUE;
}

static void part_from_channel(IdleMUCChannel *obj, const gchar *msg) {
	IdleMUCChannelPrivate *priv;
	gchar cmd[IRC_MSG_MAXLEN+1];

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	if (msg != NULL) {
		g_snprintf(cmd, IRC_MSG_MAXLEN+1, "PART %s %s", priv->channel_name, msg);
	}	else {
		g_snprintf(cmd, IRC_MSG_MAXLEN+1, "PART %s", priv->channel_name);
	}

	_idle_connection_send(priv->connection, cmd);
}

static gboolean remove_member(GObject *gobj, TpHandle handle, const gchar *message, GError **error) {
	IdleMUCChannel *obj = IDLE_MUC_CHANNEL(gobj);
	IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);
	GError *kick_error;

	if (handle == priv->connection->parent.self_handle) {
		part_from_channel(obj, message);
		return TRUE;
	}

	if (!tp_handle_set_is_member(obj->group.members, handle)) {
		IDLE_DEBUG("handle %u not a current member!", handle);

		*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "handle %u is not a current member of the channel", handle);

		return FALSE;
	}

	if (!send_kick_request(obj, handle, message, &kick_error)) {
		IDLE_DEBUG("send_kick_request failed: %s", kick_error->message);

		*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, kick_error->message);

		g_error_free(kick_error);

		return FALSE;
	}

	return TRUE;
}

/**
 * idle_muc_channel_close
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
static void idle_muc_channel_close (TpSvcChannel *obj, DBusGMethodInvocation *context) {
	IdleMUCChannelPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	if (priv->state == MUC_STATE_JOINED)
		part_from_channel((IdleMUCChannel*)(obj), NULL);

	if (priv->state < MUC_STATE_JOINED){
		if (!priv->closed) {
			tp_svc_channel_emit_closed(obj);
			priv->closed = TRUE;
		}
	}

	IDLE_DEBUG("called on %p", obj);

	tp_svc_channel_return_from_close(context);
}

/**
 * idle_muc_channel_get_channel_type
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
static void idle_muc_channel_get_channel_type (TpSvcChannel *iface, DBusGMethodInvocation *context) {
	tp_svc_channel_return_from_get_channel_type(context, TP_IFACE_CHANNEL_TYPE_TEXT);
}

/**
 * idle_muc_channel_get_handle
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
static void idle_muc_channel_get_handle (TpSvcChannel *iface, DBusGMethodInvocation *context) {
	IdleMUCChannel *obj = IDLE_MUC_CHANNEL(iface);
	IdleMUCChannelPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	tp_svc_channel_return_from_get_handle(context, TP_HANDLE_TYPE_ROOM, priv->handle);
}

/**
 * idle_muc_channel_get_interfaces
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
static void idle_muc_channel_get_interfaces (TpSvcChannel *obj, DBusGMethodInvocation *context) {
	const gchar *interfaces[] = {
		TP_IFACE_CHANNEL_INTERFACE_PASSWORD,
		TP_IFACE_CHANNEL_INTERFACE_GROUP,
		TP_IFACE_PROPERTIES_INTERFACE,
		NULL};

	tp_svc_channel_return_from_get_interfaces(context, interfaces);
}

/**
 * idle_muc_channel_get_password_flags
 *
 * Implements DBus method GetPasswordFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void idle_muc_channel_get_password_flags (TpSvcChannelInterfacePassword *iface, DBusGMethodInvocation *context) {
	IdleMUCChannel *obj = IDLE_MUC_CHANNEL(iface);
	IdleMUCChannelPrivate *priv;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	tp_svc_channel_interface_password_return_from_get_password_flags(context, priv->password_flags);
}


/**
 * idle_muc_channel_get_properties
 *
 * Implements DBus method GetProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void idle_muc_channel_get_properties (TpSvcPropertiesInterface *iface, const GArray * properties, DBusGMethodInvocation *context) {
	IdleMUCChannel *obj = IDLE_MUC_CHANNEL(iface);
	IdleMUCChannelPrivate *priv;
	GError *error;
	GPtrArray *ret;
	int i;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	for (i=0; i<properties->len; i++) {
		IdleMUCChannelTPProperty prop = g_array_index(properties, guint, i);

		if (prop >= LAST_TP_PROPERTY_ENUM) {
			IDLE_DEBUG("invalid property id %u", prop);

			error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "invalid property id %u", prop);
			dbus_g_method_return_error(context, error);
			g_error_free(error);

			return;
		}

		if (!(priv->properties[prop].flags & TP_PROPERTY_FLAG_READ)) {
			IDLE_DEBUG("not allowed to read property %u", prop);

			error = g_error_new(TP_ERRORS, TP_ERROR_PERMISSION_DENIED, "not allowed to read property %u", prop);
			dbus_g_method_return_error(context, error);
			g_error_free(error);

			return;;
		}
	}

	ret = g_ptr_array_sized_new(properties->len);

	for (i=0; i<properties->len; i++) {
		IdleMUCChannelTPProperty prop = g_array_index(properties, guint, i);
		GValue prop_val = {0, };

		g_value_init(&prop_val, TP_TYPE_PROPERTY_VALUE_STRUCT);
		g_value_take_boxed(&prop_val,
				dbus_g_type_specialized_construct(TP_TYPE_PROPERTY_VALUE_STRUCT));

		dbus_g_type_struct_set(&prop_val,
								0, prop,
								1, priv->properties[prop].value,
								G_MAXUINT);

		g_ptr_array_add(ret, g_value_get_boxed(&prop_val));
	}

	tp_svc_properties_interface_return_from_get_properties(context, ret);

	g_ptr_array_free(ret, TRUE);
}

/**
 * idle_muc_channel_list_properties
 *
 * Implements DBus method ListProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void idle_muc_channel_list_properties (TpSvcPropertiesInterface *iface, DBusGMethodInvocation *context) {
	IdleMUCChannel *obj = IDLE_MUC_CHANNEL(iface);
	IdleMUCChannelPrivate *priv;
	GError *error;
	GPtrArray *ret;
	guint i;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	ret = g_ptr_array_sized_new(LAST_TP_PROPERTY_ENUM);

	for (i=0; i<LAST_TP_PROPERTY_ENUM; i++) {
		GValue prop = {0, };
		const gchar *dbus_sig;
		const gchar *name;
		guint flags;

		switch (property_signatures[i].type) {
			case G_TYPE_BOOLEAN:
				dbus_sig = "b";
				break;

			case G_TYPE_UINT:
				dbus_sig = "u";
				break;

			case G_TYPE_STRING:
				dbus_sig = "s";
				break;

			default:
				IDLE_DEBUG("encountered unknown type %s", g_type_name(property_signatures[i].type));
				error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "internal error in %s", G_STRFUNC);
				dbus_g_method_return_error(context, error);
				g_error_free(error);
				g_ptr_array_free(ret, TRUE);

				return;
		}

		g_value_init(&prop, TP_TYPE_PROPERTY_INFO_STRUCT);
		g_value_take_boxed(&prop,
				dbus_g_type_specialized_construct(TP_TYPE_PROPERTY_INFO_STRUCT));

		name = property_signatures[i].name;
		flags = priv->properties[i].flags;

		dbus_g_type_struct_set(&prop,
				0, i,
				1, property_signatures[i].name,
				2, dbus_sig,
				3, priv->properties[i].flags,
				G_MAXUINT);

		g_ptr_array_add(ret, g_value_get_boxed(&prop));
	}

	tp_svc_properties_interface_return_from_list_properties(context, ret);

	g_ptr_array_free(ret, TRUE);
}


/**
 * idle_muc_channel_provide_password
 *
 * Implements DBus method ProvidePassword
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void idle_muc_channel_provide_password (TpSvcChannelInterfacePassword *iface, const gchar * password, DBusGMethodInvocation *context) {
	IdleMUCChannel *obj = IDLE_MUC_CHANNEL(iface);
	IdleMUCChannelPrivate *priv;
	GError *error;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	if (!(priv->password_flags & TP_CHANNEL_PASSWORD_FLAG_PROVIDE) || (priv->passwd_ctx != NULL)) {
		IDLE_DEBUG("don't need a password now or authentication already in process (handle %u)", priv->handle);

		error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "don't need a password now or authentication already in process (handle %u)", priv->handle);

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
 * Implements DBus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void idle_muc_channel_send (TpSvcChannelTypeText *iface, guint type, const gchar * text, DBusGMethodInvocation *context) {
	IdleMUCChannel *obj = (IdleMUCChannel *)(iface);
	IdleMUCChannelPrivate *priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);
	time_t timestamp = time(NULL);

	if ((priv->mode_state.flags & MODE_FLAG_MODERATED) && !(priv->mode_state.flags & (MODE_FLAG_OPERATOR_PRIVILEGE|MODE_FLAG_HALFOP_PRIVILEGE|MODE_FLAG_VOICE_PRIVILEGE))) {
		IDLE_DEBUG("emitting SEND_ERROR with (%u, %llu, %u, %s)", TP_CHANNEL_TEXT_SEND_ERROR_PERMISSION_DENIED, (guint64)(timestamp), type, text);
		tp_svc_channel_type_text_emit_send_error(iface, TP_CHANNEL_TEXT_SEND_ERROR_PERMISSION_DENIED, timestamp, type, text);
		return;
	}

	idle_text_send((GObject *)(obj), type, priv->channel_name, text, priv->connection, context);
}

static char to_irc_mode(IdleMUCChannelTPProperty prop_id) {
	switch (prop_id) {
		case TP_PROPERTY_INVITE_ONLY:
			return 'i';
		case TP_PROPERTY_MODERATED:
			return 'm';
		case TP_PROPERTY_PRIVATE:
			return 's';
		default:
			return '\0';
	}
}

static int prop_arr_find(const GPtrArray *props, IdleMUCChannelTPProperty needle) {
	int i;

	for (i=0; i<props->len; i++) {
		GValue prop = {0, };
		guint prop_id;

		g_value_init(&prop, TP_TYPE_PROPERTY_VALUE_STRUCT);
		g_value_set_static_boxed(&prop, g_ptr_array_index(props, i));

		dbus_g_type_struct_get(&prop,
							   0, &prop_id,
							   G_MAXUINT);

		if (prop_id == needle) {
			return i;
		}
	}

	return -1;
}

static void send_properties_request(IdleMUCChannel *obj, const GPtrArray *properties) {
	IdleMUCChannelPrivate *priv;
	int i;
	GPtrArray *waiting;
	gchar cmd[IRC_MSG_MAXLEN+2];
	size_t len;
	gchar *body;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));
	g_assert(properties != NULL);

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	waiting = g_ptr_array_new();

	g_snprintf(cmd, IRC_MSG_MAXLEN+2, "MODE %s ", priv->channel_name);
	len = strlen(cmd);
	body = cmd+len;

	for (i=0; i<properties->len; i++) {
		GValue prop = {0, };
		IdleMUCChannelTPProperty prop_id;
		GValue *prop_val;
		char irc_mode;

		g_value_init(&prop, TP_TYPE_PROPERTY_VALUE_STRUCT);
		g_value_set_static_boxed(&prop, g_ptr_array_index(properties, i));

		dbus_g_type_struct_get(&prop,
								0, &prop_id,
								1, &prop_val,
								G_MAXUINT);

		irc_mode = to_irc_mode(prop_id);

		if (irc_mode != '\0') {
			g_assert(G_VALUE_TYPE(prop_val) == G_TYPE_BOOLEAN);

			gboolean state = g_value_get_boolean(prop_val);

			size_t seq = 0;

			body[seq++] = state ? '+' : '-';

			body[seq++] = irc_mode;
			body[seq++] = '\0';

			_idle_connection_send(priv->connection, cmd);
		}	else {
			if (prop_id == TP_PROPERTY_SUBJECT) {
				const gchar *subject = g_value_get_string(prop_val);
				gchar cmd[IRC_MSG_MAXLEN+2];

				g_snprintf(cmd, IRC_MSG_MAXLEN+2, "TOPIC %s :%s", priv->channel_name, subject);

				_idle_connection_send(priv->connection, cmd);
			} else {
				g_ptr_array_add(waiting, g_value_get_boxed(&prop));
			}
		}
	}

	if (waiting->len) {
		int i, j;
		gpointer tmp;

		i = prop_arr_find(waiting, TP_PROPERTY_LIMITED);
		j = prop_arr_find(waiting, TP_PROPERTY_LIMIT);

		if ((i != -1) && (j != -1) && (i < j)) {
			IDLE_DEBUG("swapping order of TP_PROPERTY_LIMIT and TP_PROPERTY_LIMITED");

			tmp = g_ptr_array_index(waiting, i);
			g_ptr_array_index(waiting, i) = g_ptr_array_index(waiting, j);
			g_ptr_array_index(waiting, j) = tmp;
		}

		i = prop_arr_find(waiting, TP_PROPERTY_PASSWORD_REQUIRED);
		j = prop_arr_find(waiting, TP_PROPERTY_PASSWORD);

		if ((i != -1) && (j != -1) && (i < j)) {
			IDLE_DEBUG("swapping order of TP_PROPERTY_PASSWORD and TP_PROPERTY_PASSWORD_REQUIRED");

			tmp = g_ptr_array_index(waiting, i);
			g_ptr_array_index(waiting, i) = g_ptr_array_index(waiting, j);
			g_ptr_array_index(waiting, j) = tmp;
		}
	}

	/* okay now the data is ALWAYS before the boolean */

	for (i=0; i<waiting->len; i++) {
		GValue prop = {0, };
		IdleMUCChannelTPProperty prop_id;
		GValue *prop_val;

		g_value_init(&prop, TP_TYPE_PROPERTY_VALUE_STRUCT);
		g_value_set_static_boxed(&prop, g_ptr_array_index(waiting, i));

		dbus_g_type_struct_get(&prop,
								0, &prop_id,
								1, &prop_val,
								G_MAXUINT);

		g_assert(prop_id < LAST_TP_PROPERTY_ENUM);

		if (prop_id == TP_PROPERTY_LIMIT || prop_id == TP_PROPERTY_PASSWORD) {
			int j;

			g_value_copy(prop_val, priv->properties[prop_id].value);

			j = prop_arr_find(waiting, prop_id+1);

			if (j == -1) {
				if (prop_id == TP_PROPERTY_LIMIT && priv->mode_state.flags & MODE_FLAG_USER_LIMIT)
					g_snprintf(body, IRC_MSG_MAXLEN-len, "+l %u", g_value_get_uint(prop_val));
				else if (prop_id == TP_PROPERTY_PASSWORD && priv->mode_state.flags & MODE_FLAG_KEY)
					g_snprintf(body, IRC_MSG_MAXLEN-len, "+k %s", g_value_get_string(prop_val));
				else
					IDLE_DEBUG("%u", __LINE__);
			}
		} else if (prop_id == TP_PROPERTY_LIMITED) {
			guint limit = g_value_get_uint(priv->properties[TP_PROPERTY_LIMIT].value);

			if (g_value_get_boolean(prop_val)) {
				if (limit != 0)
					g_snprintf(body, IRC_MSG_MAXLEN-len, "+l %u", limit);
			} else {
				g_snprintf(body, IRC_MSG_MAXLEN-len, "-l");
			}
		} else if (prop_id == TP_PROPERTY_PASSWORD_REQUIRED) {
			const gchar *key = g_value_get_string(priv->properties[TP_PROPERTY_PASSWORD].value);

			if (g_value_get_boolean(prop_val)) {
				if (key != NULL)
					g_snprintf(body, IRC_MSG_MAXLEN-len, "+k %s", key);
			}	else {
				g_snprintf(body, IRC_MSG_MAXLEN-len, "-k");
			}
		}

		_idle_connection_send(priv->connection, cmd);
	}

	g_ptr_array_free(waiting, TRUE);
}

/**
 * idle_muc_channel_set_properties
 *
 * Implements DBus method SetProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void idle_muc_channel_set_properties (TpSvcPropertiesInterface *iface, const GPtrArray * properties, DBusGMethodInvocation *context) {
	IdleMUCChannel *obj = IDLE_MUC_CHANNEL(iface);
	IdleMUCChannelPrivate *priv;
	GPtrArray *to_change;
	GError *error;
	int i;

	g_assert(obj != NULL);
	g_assert(IDLE_IS_MUC_CHANNEL(obj));

	priv = IDLE_MUC_CHANNEL_GET_PRIVATE(obj);

	to_change = g_ptr_array_new();

	for (i=0; i<properties->len; i++) {
		GValue prop = {0, };
		IdleMUCChannelTPProperty prop_id;
		GValue *prop_val;

		g_value_init(&prop, TP_TYPE_PROPERTY_VALUE_STRUCT);
		g_value_set_static_boxed(&prop, g_ptr_array_index(properties, i));

		dbus_g_type_struct_get(&prop,
				0, &prop_id,
				1, &prop_val,
				G_MAXUINT);

		if (prop_id >= LAST_TP_PROPERTY_ENUM) {
			IDLE_DEBUG("invalid property id %u", prop_id);

			error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "invalid property id %u", prop_id);
			dbus_g_method_return_error(context, error);
			g_error_free(error);
			g_ptr_array_free(to_change, TRUE);

			return;
		}

		if ((priv->properties[prop_id].flags & TP_PROPERTY_FLAG_WRITE) == 0) {
			IDLE_DEBUG("not allowed to set property with id %u", prop_id);

			error = g_error_new(TP_ERRORS, TP_ERROR_PERMISSION_DENIED, "not allowed to set property with id %u", prop_id);
			dbus_g_method_return_error(context, error);
			g_error_free(error);
			g_ptr_array_free(to_change, TRUE);

			return;
		}

		if (!g_value_type_compatible(G_VALUE_TYPE(prop_val), property_signatures[prop_id].type)) {
			IDLE_DEBUG("incompatible value type %s for prop_id %u", g_type_name(G_VALUE_TYPE(prop_val)), prop_id);

			error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "incompatible value type %s for prop_id %u", g_type_name(G_VALUE_TYPE(prop_val)), prop_id);
			dbus_g_method_return_error(context, error);
			g_error_free(error);
			g_ptr_array_free(to_change, TRUE);

			return;
		}

		if (!g_value_compare(prop_val, priv->properties[prop_id].value)) {
			g_ptr_array_add(to_change, g_value_get_boxed(&prop));
		}
	}

	send_properties_request(obj, to_change);

	g_ptr_array_free(to_change, TRUE);

	tp_svc_properties_interface_return_from_set_properties(context);
}

gboolean idle_muc_channel_is_modechar(char c) {
	switch (c) {
		case '@':
		case '&':
		case '+':
		case '~':
			return TRUE;

		default:
			return FALSE;
	}
}

static void
channel_iface_init(gpointer g_iface, gpointer iface_data) {
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, idle_muc_channel_##x)
  IMPLEMENT(close);
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}

static void
password_iface_init(gpointer g_iface, gpointer iface_data) {
	TpSvcChannelInterfacePasswordClass *klass = (TpSvcChannelInterfacePasswordClass *)(g_iface);

#define IMPLEMENT(x) tp_svc_channel_interface_password_implement_##x (\
		klass, idle_muc_channel_##x)
	IMPLEMENT(get_password_flags);
	IMPLEMENT(provide_password);
#undef IMPLEMENT
}

static void
properties_iface_init(gpointer g_iface, gpointer iface_data) {
	TpSvcPropertiesInterfaceClass *klass = (TpSvcPropertiesInterfaceClass *)(g_iface);

#define IMPLEMENT(x) tp_svc_properties_interface_implement_##x (\
		klass, idle_muc_channel_##x)
	IMPLEMENT(get_properties);
	IMPLEMENT(list_properties);
	IMPLEMENT(set_properties);
#undef IMPLEMENT
}

static void
text_iface_init(gpointer g_iface, gpointer iface_data) {
	TpSvcChannelTypeTextClass *klass = (TpSvcChannelTypeTextClass *)(g_iface);

  tp_text_mixin_iface_init(g_iface, iface_data);
#define IMPLEMENT(x) tp_svc_channel_type_text_implement_##x (\
    klass, idle_muc_channel_##x)
  IMPLEMENT(send);
#undef IMPLEMENT
}

