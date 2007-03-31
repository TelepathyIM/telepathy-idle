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

#include "idle-parser.h"

#include "idle-connection.h"
#include "idle-muc-channel.h"

#include <glib.h>
#include <glib-object.h>

#include <telepathy-glib/intset.h>

#define __USE_GNU
#include <string.h>

#define IDLE_PARSER_GET_PRIVATE(parser) (G_TYPE_INSTANCE_GET_PRIVATE((parser), IDLE_TYPE_PARSER, IdleParserPrivate))

G_DEFINE_TYPE(IdleParser, idle_parser, G_TYPE_OBJECT);

/* signals */
enum {
	SIGNAL_MSG_SPLIT = 0,
	LAST_SIGNAL_ENUM
};

/* properties */
enum {
	PROP_CONNECTION = 1,
	LAST_PROPERTY_ENUM
};

static guint signals[LAST_SIGNAL_ENUM] = {0};

static const gchar *message_formats[IDLE_PARSER_LAST_MESSAGE_CODE] = {
	"Is", /* CMD_PING */

	"cIcr", /* PREFIXCMD_INVITE */
	"cIr", /* PREFIXCMD_JOIN */
	"cIrc.", /* PREFIXCMD_KICK */
	"cIrvs", /* PREFIXCMD_MODE_CHANNEL */
	"cIcvs", /* PREFIXCMD_MODE_USER */
	"cIcr", /* PREFIXCMD_NICK */
	"cIr:", /* PREFIXCMD_NOTICE_CHANNEL */
	"cIc:", /* PREFIXCMD_NOTICE_USER */
	"cIr.", /* PREFIXCMD_PART */
	"cIr:", /* PREFIXCMD_PRIVMSG_CHANNEL */
	"cIc:", /* PREFIXCMD_PRIVMSG_USER */
	"cI.", /* PREFIXCMD_QUIT */
	"cIr:", /* PREFIXCMD_TOPIC */

	"IIIc:", /* NUMERIC_AWAY */
	"IIIr", /* NUMERIC_BADCHANNELKEY */
	"IIIr", /* NUMERIC_BANNEDFROMCHAN */
	"IIIr", /* NUMERIC_CANNOTSENDTOCHAN */
	"IIIr", /* NUMERIC_CHANNELISFULL */
	"IIIc", /* NUMERIC_ENDOFWHOIS */
	"III", /* NUMERIC_ERRONEOUSNICKNAME */
	"IIIr", /* NUMERIC_INVITEONLYCHAN */
	"IIIrvs", /* NUMERIC_MODEREPLY */
	"IIIIrvC", /* NUMERIC_NAMEREPLY */
	"III", /* NUMERIC_NICKNAMEINUSE */
	"IIIc", /* NUMERIC_NOSUCHNICK */
	"III", /* NUMERIC_NOWAWAY */
	"IIIr:", /* NUMERIC_TOPIC */
	"IIIrcd", /* NUMERIC_TOPIC_STAMP */
	"III", /* NUMERIC_UNAWAY */
	"IIc", /* NUMERIC_WELCOME */
	"IIIcd", /* NUMERIC_WHOISIDLE */
};

typedef struct _MessageSpec MessageSpec;
struct _MessageSpec {
	const gchar *str;
	IdleParserMessageCode code;
};

const static MessageSpec message_specs[] = {
	{"PING", IDLE_PARSER_CMD_PING},

	{"INVITE", IDLE_PARSER_PREFIXCMD_INVITE},
	{"JOIN", IDLE_PARSER_PREFIXCMD_JOIN},
	{"KICK", IDLE_PARSER_PREFIXCMD_KICK},
	{"MODE", IDLE_PARSER_PREFIXCMD_MODE_CHANNEL},
	{"MODE", IDLE_PARSER_PREFIXCMD_MODE_USER},

	{"NICK", IDLE_PARSER_PREFIXCMD_NICK},
	{"NOTICE", IDLE_PARSER_PREFIXCMD_NOTICE_CHANNEL},
	{"NOTICE", IDLE_PARSER_PREFIXCMD_NOTICE_USER},
	{"PART", IDLE_PARSER_PREFIXCMD_PART},
	{"PRIVMSG", IDLE_PARSER_PREFIXCMD_PRIVMSG_CHANNEL},
	{"PRIVMSG", IDLE_PARSER_PREFIXCMD_PRIVMSG_USER},
	{"QUIT", IDLE_PARSER_PREFIXCMD_QUIT},
	{"TOPIC", IDLE_PARSER_PREFIXCMD_TOPIC},

	{"301", IDLE_PARSER_NUMERIC_AWAY},
	{"475", IDLE_PARSER_NUMERIC_BADCHANNELKEY},
	{"474", IDLE_PARSER_NUMERIC_BANNEDFROMCHAN},
	{"404", IDLE_PARSER_NUMERIC_CANNOTSENDTOCHAN},
	{"471", IDLE_PARSER_NUMERIC_CHANNELISFULL},
	{"318", IDLE_PARSER_NUMERIC_ENDOFWHOIS},
	{"432", IDLE_PARSER_NUMERIC_ERRONEOUSNICKNAME},
	{"473", IDLE_PARSER_NUMERIC_INVITEONLYCHAN},
	{"324", IDLE_PARSER_NUMERIC_MODEREPLY},
	{"353", IDLE_PARSER_NUMERIC_NAMEREPLY},
	{"433", IDLE_PARSER_NUMERIC_NICKNAMEINUSE},
	{"401", IDLE_PARSER_NUMERIC_NOSUCHNICK},
	{"306", IDLE_PARSER_NUMERIC_NOWAWAY},
	{"332", IDLE_PARSER_NUMERIC_TOPIC},
	{"333", IDLE_PARSER_NUMERIC_TOPIC_STAMP},
	{"305", IDLE_PARSER_NUMERIC_UNAWAY},
	{"001", IDLE_PARSER_NUMERIC_WELCOME},
	{"317", IDLE_PARSER_NUMERIC_WHOISIDLE},

	{NULL, IDLE_PARSER_LAST_MESSAGE_CODE}
};

typedef struct _MessageHandlerClosure MessageHandlerClosure;
struct _MessageHandlerClosure {
	IdleParserMessageHandler handler;
	gpointer user_data;
};

static MessageHandlerClosure *_message_handler_closure_new(IdleParserMessageHandler handler, gpointer user_data) {
	MessageHandlerClosure *closure = g_slice_new(MessageHandlerClosure);

	closure->handler = handler;
	closure->user_data = user_data;

	return closure;
}

typedef struct _IdleParserPrivate IdleParserPrivate;
struct _IdleParserPrivate {
	/* connection object (for handle repos) */
	IdleConnection *conn;

	/* continuation line buffer */
	gchar split_buf[IRC_MSG_MAXLEN+3];

	/* message handlers */
	GSList *handlers[IDLE_PARSER_LAST_MESSAGE_CODE];
};

static void idle_parser_init(IdleParser *obj) {
}

static void idle_parser_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec) {
	IdleParserPrivate *priv = IDLE_PARSER_GET_PRIVATE(obj);

	if (prop_id == PROP_CONNECTION) {
		priv->conn = g_value_get_object(value);
	} else {
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
	}
}

static void idle_parser_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec) {
	IdleParserPrivate *priv = IDLE_PARSER_GET_PRIVATE(obj);

	if (prop_id == PROP_CONNECTION) {
		g_value_set_object(value, priv->conn);
	} else {
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
	}
}

static void idle_parser_finalize(GObject *obj) {
	IdleParserPrivate *priv = IDLE_PARSER_GET_PRIVATE(obj);
	int i;

	for (i = 0; i < IDLE_PARSER_LAST_MESSAGE_CODE; i++) {
		GSList *link;

		for (link = priv->handlers[i]; link != NULL; link = link->next)
			g_free(link->data);

		g_slist_free(priv->handlers[i]);
	}
}

static void idle_parser_class_init(IdleParserClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	g_type_class_add_private(klass, sizeof(IdleParserPrivate));

	object_class->set_property = idle_parser_set_property;
	object_class->get_property = idle_parser_get_property;

	object_class->finalize = idle_parser_finalize;

	g_object_class_install_property(object_class, PROP_CONNECTION, g_param_spec_object("connection", "IdleConnection object", "The IdleConnection object of which handle repos this IdleParser object uses", IDLE_TYPE_CONNECTION, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

	signals[SIGNAL_MSG_SPLIT] = g_signal_new("msg-split", G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__STRING, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void _parse_message(IdleParser *parser, const gchar *split_msg);
static void _parse_and_forward_one(IdleParser *parser, gchar **tokens, IdleParserMessageCode code, TpIntSet *contact_handles, TpIntSet *room_handles);
static gboolean _parse_atom(IdleParser *parser, GValueArray *arr, char atom, const gchar *token, TpIntSet *contact_handles, TpIntSet *room_handles);

void idle_parser_receive(IdleParser *parser, const gchar *msg) {
	IdleParserPrivate *priv = IDLE_PARSER_GET_PRIVATE(parser);
	int i;
	int lasti = 0;
	gchar *tmp;
	gboolean line_ends = FALSE;
	guint len;
	gchar concat_buf[2 * (IRC_MSG_MAXLEN + 3)] = {'\0'};

	g_assert(msg != NULL);

	len = strnlen(msg, IRC_MSG_MAXLEN + 3);

	for (i = 0; i < len; i++) {
		if ((msg[i] == '\n' || msg[i] == '\r')) {
			if (i > lasti) {
				if ((lasti == 0) && (priv->split_buf[0] != '\0')) {
					g_strlcpy(g_stpcpy(concat_buf, priv->split_buf), msg, i);
					tmp = concat_buf;
					memset(priv->split_buf, '\0', IRC_MSG_MAXLEN + 3);
				}	else {
					tmp = g_strndup(msg + lasti, i - lasti);
				}

				g_signal_emit(parser, signals[SIGNAL_MSG_SPLIT], 0, tmp);
				_parse_message(parser, tmp);

				if (tmp != concat_buf)
					g_free(tmp);
			}

			lasti = i+1;
			line_ends = TRUE;
		}	else {
			line_ends = FALSE;
		}
	}

	if (!line_ends)
		g_strlcpy(priv->split_buf, msg + lasti, (IRC_MSG_MAXLEN + 3) - lasti);
	else
		memset(priv->split_buf, '\0', IRC_MSG_MAXLEN + 3);
}

void idle_parser_add_handler(IdleParser *parser, IdleParserMessageCode code, IdleParserMessageHandler handler, gpointer user_data) {
	IdleParserPrivate *priv = IDLE_PARSER_GET_PRIVATE(parser);

	if (code >= IDLE_PARSER_LAST_MESSAGE_CODE)
		return;

	priv->handlers[code] = g_slist_append(priv->handlers[code], _message_handler_closure_new(handler, user_data));
}

static gint _data_compare_func(gconstpointer a, gconstpointer b) {
	const MessageHandlerClosure *_a = a, *_b = b;

	return (_a->user_data == _b->user_data) ? 0 : 1;
}

void idle_parser_remove_handlers_by_data(IdleParser *parser, gpointer user_data) {
	IdleParserPrivate *priv = IDLE_PARSER_GET_PRIVATE(parser);
	int i;

	for (i = 0; i < IDLE_PARSER_LAST_MESSAGE_CODE; i++) {
		GSList *link;

		while ((link = g_slist_find_custom(priv->handlers[i], user_data, _data_compare_func)))
			priv->handlers[i] = g_slist_remove_link(priv->handlers[i], link);
	}
}

static gchar **_tokenize(const gchar *str) {
	GArray *array = g_array_new(TRUE, TRUE, sizeof(gchar *));
	gchar **ret;
	const gchar *iter, *last;

	iter = last = str;
	while (iter && *iter) {
		iter = strchr(last, ' ');

		if (iter) {
			if ((iter - last) > 0) {
				const gchar * vals[] = {g_strndup(last, iter - last), last};
				g_array_append_vals(array, vals, 2);
			}
		} else {
			const gchar * vals[] = {g_strdup(last), last};
			g_array_append_vals(array, vals, 2);
		}

		last = iter + 1;
	}

	ret = (gchar **) array->data;
	g_array_free(array, FALSE);
	return ret;
}

static void _free_tokens(gchar **tokens) {
	gchar **token;

	if (!tokens)
		return;

	for (token = tokens; *token; token += 2)
		g_free(*token);

	g_free(tokens);
}

#undef __USE_GNU
#include <stdio.h>

static void _unref_one(guint i, gpointer user_data) {
	TpHandleRepoIface *iface = (TpHandleRepoIface *) user_data;
	tp_handle_unref(iface, i);
}

static void _parse_message(IdleParser *parser, const gchar *split_msg) {
	IdleParserPrivate *priv = IDLE_PARSER_GET_PRIVATE(parser);
	int i;
	gchar **tokens = _tokenize(split_msg);
	g_debug("%s: parsing \"%s\"", G_STRFUNC, split_msg);

	for (i = 0; i < IDLE_PARSER_LAST_MESSAGE_CODE; i++) {
		TpIntSet *contact_handles = tp_intset_new(), *room_handles = tp_intset_new();
		const MessageSpec *spec = &(message_specs[i]);

		if (split_msg[0] != ':') {
			if (!g_ascii_strcasecmp(tokens[0], spec->str))
				_parse_and_forward_one(parser, tokens, spec->code, contact_handles, room_handles);
		} else {
			if (!g_ascii_strcasecmp(tokens[2], spec->str))
				_parse_and_forward_one(parser, tokens, spec->code, contact_handles, room_handles);
		}

		tp_intset_foreach(contact_handles, _unref_one, priv->conn->handles[TP_HANDLE_TYPE_CONTACT]);
		tp_intset_foreach(room_handles, _unref_one, priv->conn->handles[TP_HANDLE_TYPE_ROOM]);

		tp_intset_destroy(contact_handles);
		tp_intset_destroy(room_handles);
	}

	_free_tokens(tokens);
}

static void _parse_and_forward_one(IdleParser *parser, gchar **tokens, IdleParserMessageCode code, TpIntSet *contact_handles, TpIntSet *room_handles) {
	IdleParserPrivate *priv = IDLE_PARSER_GET_PRIVATE(parser);
	GValueArray *args = g_value_array_new(3);
	GSList *link = priv->handlers[code];
	IdleParserHandlerResult result = IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	const gchar *format = message_formats[code];
	gboolean success = TRUE;
	gchar **iter = tokens;

	g_debug("%s: message code %u", G_STRFUNC, code);

	while ((*format != '\0') && success && (*iter != NULL)) {
		GValue val = {0};

		if (*format == 'v') {
			format++;
			while (*iter != NULL) {
				if (!_parse_atom(parser, args, *format, iter[0], contact_handles, room_handles)) {
					success = FALSE;
					break;
				}

				iter += 2;
			}
		} else if ((*format == ':') || (*format == '.')) {
			if (iter[0][0] != ':') {
				success = FALSE;
				break;
			}

			g_value_init(&val, G_TYPE_STRING);
			g_value_set_string(&val, iter[1] + 1);
			g_value_array_append(args, &val);
			g_value_unset(&val);

			g_debug("%s: set string \"%s\"", G_STRFUNC, iter[1] + 1);
		} else {
			if (!_parse_atom(parser, args, *format, iter[0], contact_handles, room_handles)) {
				success = FALSE;
				break;
			}
		}

		format++;
		iter += 2;
	}

	if (!success) {
		g_debug("%s: failed to parse \"%s\"", G_STRFUNC, tokens[1]);

		g_value_array_free(args);
		return;
	}

	if (*format && (*format != '.')) {
		g_debug("%s: missing args in message \"%s\"", G_STRFUNC, tokens[1]);

		g_value_array_free(args);
		return;
	}

	g_debug("%s: succesfully parsed", G_STRFUNC);

	while (link) {
		MessageHandlerClosure *closure = link->data;
		result = closure->handler(parser, code, args, closure->user_data);
		if (result == IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED) {
			link = link->next;
		} else if (result == IDLE_PARSER_HANDLER_RESULT_HANDLED) {
			break;
		} else if (result == IDLE_PARSER_HANDLER_RESULT_NO_MORE_PLEASE) {
			GSList *tmp = link->next;
			g_free(closure);
			priv->handlers[code] = g_slist_remove_link(priv->handlers[code], link);
			link = tmp;
		} else {
			g_assert_not_reached();
		}
	}

	g_value_array_free(args);
}

static gboolean _parse_atom(IdleParser *parser, GValueArray *arr, char atom, const gchar *token, TpIntSet *contact_handles, TpIntSet *room_handles) {
	IdleParserPrivate *priv = IDLE_PARSER_GET_PRIVATE(parser);
	TpHandleRepoIface **handles = priv->conn->handles;
	TpHandle handle;
	GValue val = {0};

	if (token[0] == ':')
		token++;

	switch (atom) {
		case 'I':
			g_debug("%s: ignored token", G_STRFUNC);
			return TRUE;
			break;

		case 'c':
		case 'r':
		case 'C': {
			gchar *id, *bang = NULL;
			gchar modechar = '\0';

			if ((atom == 'C') && idle_muc_channel_is_modechar(token[0])) {
				modechar = token[0];
				token++;
			}

			id = g_strdup(token);
			bang = strchr(id, '!');

			if (bang)
				*bang = '\0';

			if (atom == 'r') {
				if ((handle = idle_handle_for_room(handles[TP_HANDLE_TYPE_ROOM], id)))
					tp_intset_add(room_handles, handle);
			} else {
				if ((handle = idle_handle_for_contact(handles[TP_HANDLE_TYPE_CONTACT], id)))
					tp_intset_add(contact_handles, handle);
			}

			g_free(id);

			if (!handle)
				return FALSE;

			g_value_init(&val, G_TYPE_UINT);
			g_value_set_uint(&val, handle);
			g_value_array_append(arr, &val);
			g_value_unset(&val);

			g_debug("%s: set handle %u", G_STRFUNC, handle);

			if (modechar != '\0') {
				g_value_init(&val, G_TYPE_CHAR);
				g_value_set_char(&val, modechar);
				g_value_array_append(arr, &val);
				g_value_unset(&val);

				g_debug("%s: set modechar %c", G_STRFUNC, modechar);
			}

			return TRUE;
		}
		break;

		case 'd': {
			guint dval;

			if (sscanf(token, "%d", &dval)) {
				g_value_init(&val, G_TYPE_UINT);
				g_value_set_uint(&val, dval);
				g_value_array_append(arr, &val);
				g_value_unset(&val);

				g_debug("%s: set int %d", G_STRFUNC, dval);

				return TRUE;
			} else {
				return FALSE;
			}
		}
		break;

		case 's':
			g_value_init(&val, G_TYPE_STRING);
			g_value_set_string(&val, token);
			g_value_array_append(arr, &val);
			g_value_unset(&val);
			g_debug("%s: set string \"%s\"", G_STRFUNC, token);

			return TRUE;
			break;

		default:
			g_debug("%s: unknown atom %c", G_STRFUNC, atom);
			return FALSE;
			break;
	}
}
