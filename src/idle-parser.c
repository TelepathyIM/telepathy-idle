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

/* For strnlen(), which is a GNU extension. */
#define  _GNU_SOURCE

#include "config.h"
#include "idle-parser.h"

#include "idle-connection.h"
#include "idle-muc-channel.h"

#include <glib.h>
#include <glib-object.h>

#include <string.h>
#include <stdio.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_PARSER
#include "idle-debug.h"

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

typedef struct _MessageSpec MessageSpec;
struct _MessageSpec {
	const gchar *str;
	const gchar *format;
	IdleParserMessageCode code;
};

/* Message spec key:
 * 'I' - ignore token
 * 'r' - token is a room name
 * 'c' - token is a contact (nick)
 * 'C' - token is a contact (nick) with mode characters
 * 'v' - following token is repeated multiple times
 * 's' - token is a string
 * ':' - Consume all remaining tokens as a single string prefixed by ':'
 *         (e.g. ':this is a message string')
 * '.' - Same as ':', but optional
 */
static const MessageSpec message_specs[] = {
	{"ERROR", "I:", IDLE_PARSER_CMD_ERROR},
	{"PING", "Is", IDLE_PARSER_CMD_PING},

	{"INVITE", "cIcr", IDLE_PARSER_PREFIXCMD_INVITE},
	{"JOIN", "cIr", IDLE_PARSER_PREFIXCMD_JOIN},
	{"KICK", "cIrc.", IDLE_PARSER_PREFIXCMD_KICK},
	{"MODE", "IIrvs", IDLE_PARSER_PREFIXCMD_MODE_CHANNEL},
	{"MODE", "IIcvs", IDLE_PARSER_PREFIXCMD_MODE_USER},
	{"NICK", "cIc", IDLE_PARSER_PREFIXCMD_NICK},
	{"NOTICE", "cIr:", IDLE_PARSER_PREFIXCMD_NOTICE_CHANNEL},
	{"NOTICE", "cIc:", IDLE_PARSER_PREFIXCMD_NOTICE_USER},
	{"PART", "cIr.", IDLE_PARSER_PREFIXCMD_PART},
	{"PONG", "IIs.", IDLE_PARSER_PREFIXCMD_PONG},
	{"PRIVMSG", "cIr:", IDLE_PARSER_PREFIXCMD_PRIVMSG_CHANNEL},
	{"PRIVMSG", "cIc:", IDLE_PARSER_PREFIXCMD_PRIVMSG_USER},
	{"QUIT", "cI.", IDLE_PARSER_PREFIXCMD_QUIT},
	{"TOPIC", "cIr.", IDLE_PARSER_PREFIXCMD_TOPIC},

	{"301", "IIIc:", IDLE_PARSER_NUMERIC_AWAY},
	{"475", "IIIr", IDLE_PARSER_NUMERIC_BADCHANNELKEY},
	{"474", "IIIr", IDLE_PARSER_NUMERIC_BANNEDFROMCHAN},
	{"404", "IIIr", IDLE_PARSER_NUMERIC_CANNOTSENDTOCHAN},
	{"471", "IIIr", IDLE_PARSER_NUMERIC_CHANNELISFULL},
	{"318", "IIIc", IDLE_PARSER_NUMERIC_ENDOFWHOIS},
	{"432", "III", IDLE_PARSER_NUMERIC_ERRONEOUSNICKNAME},
	{"473", "IIIr", IDLE_PARSER_NUMERIC_INVITEONLYCHAN},
	{"324", "IIIrvs", IDLE_PARSER_NUMERIC_MODEREPLY},
	{"353", "IIIIrvC", IDLE_PARSER_NUMERIC_NAMEREPLY},
	{"366", "IIIr", IDLE_PARSER_NUMERIC_NAMEREPLY_END},
	{"433", "III", IDLE_PARSER_NUMERIC_NICKNAMEINUSE},
	{"401", "IIIc", IDLE_PARSER_NUMERIC_NOSUCHNICK},
	{"402", "IIIs:", IDLE_PARSER_NUMERIC_NOSUCHSERVER},
	{"306", "III", IDLE_PARSER_NUMERIC_NOWAWAY},
	{"332", "IIIr:", IDLE_PARSER_NUMERIC_TOPIC},
	{"333", "IIIrcd", IDLE_PARSER_NUMERIC_TOPIC_STAMP},
	{"263", "IIIs:", IDLE_PARSER_NUMERIC_TRYAGAIN},
	{"305", "III", IDLE_PARSER_NUMERIC_UNAWAY},
	{"001", "IIc", IDLE_PARSER_NUMERIC_WELCOME},
	{"319", "IIIc.", IDLE_PARSER_NUMERIC_WHOISCHANNELS},
	{"378", "IIIc:", IDLE_PARSER_NUMERIC_WHOISHOST},
	{"330", "IIIcs:", IDLE_PARSER_NUMERIC_WHOISLOGGEDIN},
	{"313", "IIIc:", IDLE_PARSER_NUMERIC_WHOISOPERATOR},
	{"307", "IIIc:", IDLE_PARSER_NUMERIC_WHOISREGNICK},
	{"671", "IIIc:", IDLE_PARSER_NUMERIC_WHOISSECURE},
	{"312", "IIIcs:", IDLE_PARSER_NUMERIC_WHOISSERVER},
	{"311", "IIIcssI:", IDLE_PARSER_NUMERIC_WHOISUSER},
	{"317", "IIIcd", IDLE_PARSER_NUMERIC_WHOISIDLE},
	{"322", "IIIrd.", IDLE_PARSER_NUMERIC_LIST},
	{"323", "I", IDLE_PARSER_NUMERIC_LISTEND},
	{"421", "IIIs:", IDLE_PARSER_NUMERIC_UNKNOWNCOMMAND},

	{NULL, NULL, IDLE_PARSER_LAST_MESSAGE_CODE}
};

typedef struct _MessageHandlerClosure MessageHandlerClosure;
struct _MessageHandlerClosure {
	IdleParserMessageHandler handler;
	gpointer user_data;
	guint priority;
};

static MessageHandlerClosure *_message_handler_closure_new(IdleParserMessageHandler handler, gpointer user_data, IdleParserHandlerPriority priority) {
	MessageHandlerClosure *closure = g_slice_new(MessageHandlerClosure);

	closure->handler = handler;
	closure->user_data = user_data;
	closure->priority = priority;

	return closure;
}

typedef struct _IdleParserPrivate IdleParserPrivate;
struct _IdleParserPrivate {
	/* connection object (for handle repos) */
	IdleConnection *conn;

	/* continuation line buffer */
	gchar split_buf[IRC_MSG_MAXLEN + 3];

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
		GSList *link_;

		for (link_ = priv->handlers[i]; link_ != NULL; link_ = link_->next)
			g_slice_free(MessageHandlerClosure, link_->data);

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
static void _parse_and_forward_one(IdleParser *parser, gchar **tokens, IdleParserMessageCode code, const gchar *format);
static gboolean _parse_atom(IdleParser *parser, GValueArray *arr, char atom, const gchar *token, TpHandleSet *contact_reffed, TpHandleSet *room_reffed);

#ifndef HAVE_STRNLEN
static size_t
strnlen(const char *msg, size_t maxlen)
{
	size_t i;

	for (i=0; i<maxlen; i++)
		if (msg[i] == '\0')
			break;

	return i;
}
#endif

void idle_parser_receive(IdleParser *parser, const gchar *msg) {
	IdleParserPrivate *priv = IDLE_PARSER_GET_PRIVATE(parser);
	guint i;
	guint lasti = 0;
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
					g_strlcpy(g_stpcpy(concat_buf, priv->split_buf), msg, i + 1);
					tmp = concat_buf;
					memset(priv->split_buf, '\0', IRC_MSG_MAXLEN + 3);
				} else {
					tmp = g_strndup(msg + lasti, i - lasti);
				}

				g_signal_emit(parser, signals[SIGNAL_MSG_SPLIT], 0, tmp);
				_parse_message(parser, tmp);

				if (tmp != concat_buf)
					g_free(tmp);
			}

			lasti = i + 1;
			line_ends = TRUE;
		} else {
			line_ends = FALSE;
		}
	}

	if (!line_ends)
		g_strlcpy(priv->split_buf, msg + lasti, (IRC_MSG_MAXLEN + 3) - lasti);
	else
		memset(priv->split_buf, '\0', IRC_MSG_MAXLEN + 3);
}

void idle_parser_add_handler(IdleParser *parser, IdleParserMessageCode code, IdleParserMessageHandler handler, gpointer user_data) {
	idle_parser_add_handler_with_priority(parser, code, handler, user_data, IDLE_PARSER_HANDLER_PRIORITY_DEFAULT);
	return;
}

static gint _message_handler_closure_priority_compare(gconstpointer a, gconstpointer b) {
	const MessageHandlerClosure *_a = a, *_b = b;

	return (_a->priority == _b->priority) ? 0 : (_a->priority < _b->priority) ? -1 : 1;
}

void idle_parser_add_handler_with_priority(IdleParser *parser, IdleParserMessageCode code, IdleParserMessageHandler handler, gpointer user_data, IdleParserHandlerPriority priority) {
	IdleParserPrivate *priv = IDLE_PARSER_GET_PRIVATE(parser);

	if (code >= IDLE_PARSER_LAST_MESSAGE_CODE)
		return;

	priv->handlers[code] = g_slist_insert_sorted(priv->handlers[code], _message_handler_closure_new(handler, user_data, priority), _message_handler_closure_priority_compare);
}

static gint _message_handler_closure_user_data_compare(gconstpointer a, gconstpointer b) {
	const MessageHandlerClosure *_a = a, *_b = b;

	return (_a->user_data == _b->user_data) ? 0 : 1;
}

void idle_parser_remove_handlers_by_data(IdleParser *parser, gpointer user_data) {
	IdleParserPrivate *priv = IDLE_PARSER_GET_PRIVATE(parser);
	int i;

	for (i = 0; i < IDLE_PARSER_LAST_MESSAGE_CODE; i++) {
		GSList *link_;

		while ((link_ = g_slist_find_custom(priv->handlers[i], user_data, _message_handler_closure_user_data_compare)))
			priv->handlers[i] = g_slist_remove_link(priv->handlers[i], link_);
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
		} else if (*last != '\0') {
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

static void _parse_message(IdleParser *parser, const gchar *split_msg) {
	gchar **tokens = _tokenize(split_msg);
	IDLE_DEBUG("parsing \"%s\"", split_msg);

	for (int i = 0; i < IDLE_PARSER_LAST_MESSAGE_CODE; i++) {
		const MessageSpec *spec = &(message_specs[i]);

		if ((split_msg[0] != ':') && (i <= IDLE_PARSER_LAST_NON_PREFIX_CMD)) {
			if (!g_ascii_strcasecmp(tokens[0], spec->str))
				_parse_and_forward_one(parser, tokens, spec->code, spec->format);
		} else if (i > IDLE_PARSER_LAST_NON_PREFIX_CMD) {
			if (!g_ascii_strcasecmp(tokens[2], spec->str))
				_parse_and_forward_one(parser, tokens, spec->code, spec->format);
		}
	}

	_free_tokens(tokens);
}

static void _parse_and_forward_one(IdleParser *parser, gchar **tokens, IdleParserMessageCode code, const gchar *format) {
	IdleParserPrivate *priv = IDLE_PARSER_GET_PRIVATE(parser);
	GValueArray *args = g_value_array_new(3);
	GSList *link_ = priv->handlers[code];
	IdleParserHandlerResult result = IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
	gboolean success = TRUE;
	gchar **iter = tokens;
	/* We keep a ref to each unique handle in a message so that we can unref them after calling all handlers */
	TpHandleSet *contact_reffed = tp_handle_set_new(tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->conn), TP_HANDLE_TYPE_CONTACT));
	TpHandleSet *room_reffed = tp_handle_set_new(tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->conn), TP_HANDLE_TYPE_ROOM));

	IDLE_DEBUG("message code %u", code);

	while ((*format != '\0') && success && (*iter != NULL)) {
		GValue val = {0};

		if (*format == 'v') {
			format++;
			while (*iter != NULL) {
				if (!_parse_atom(parser, args, *format, iter[0], contact_reffed, room_reffed)) {
					success = FALSE;
					break;
				}

				iter += 2;
			}
		} else if ((*format == ':') || (*format == '.')) {
			/* Assume the happy case of the trailing parameter starting after the :
			 * in the trailing string as the RFC intended */
			const gchar *trailing = iter[1] + 1;

			/* Some IRC proxies *cough* bip *cough* omit the : in the trailing
			 * parameter if that parameter is just one word, to cope with that check
			 * if there are no more tokens after the current one and if so, accept a
			 * trailing string without the : prefix. */
			if (iter[0][0] != ':') {
				if (iter[2] == NULL) {
					trailing = iter[1];
				} else {
					success = FALSE;
					break;
				}
			}

			/*
			 * because of the way things are tokenized, if there is a
			 * space immediately after the the ':', the current token will only be
			 * ":", so we check that the trailing string is non-NULL rather than
			 * checking iter[0][1] (since iter[0] is a NULL-terminated token string
			 * whereas trailing is a pointer into the full message string
			 */
			if (trailing[0] == '\0') {
				success = FALSE;
				break;
			}

			g_value_init(&val, G_TYPE_STRING);
			g_value_set_string(&val, trailing);
			g_value_array_append(args, &val);
			g_value_unset(&val);

			IDLE_DEBUG("set string \"%s\"", trailing);
		} else {
			if (!_parse_atom(parser, args, *format, iter[0], contact_reffed, room_reffed)) {
				success = FALSE;
				break;
			}
		}

		format++;
		iter += 2;
	}

	if (!success && (*format != '.')) {
		IDLE_DEBUG("failed to parse \"%s\"", tokens[1]);

		goto cleanup;
	}

	if (*format && (*format != '.')) {
		IDLE_DEBUG("missing args in message \"%s\"", tokens[1]);

		goto cleanup;
	}

	IDLE_DEBUG("successfully parsed");

	while (link_) {
		MessageHandlerClosure *closure = link_->data;
		result = closure->handler(parser, code, args, closure->user_data);
		if (result == IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED) {
			link_ = link_->next;
		} else if (result == IDLE_PARSER_HANDLER_RESULT_HANDLED) {
			break;
		} else if (result == IDLE_PARSER_HANDLER_RESULT_NO_MORE_PLEASE) {
			GSList *tmp = link_->next;
			g_free(closure);
			priv->handlers[code] = g_slist_remove_link(priv->handlers[code], link_);
			link_ = tmp;
		} else {
			g_assert_not_reached();
		}
	}

cleanup:

	g_value_array_free(args);

	tp_handle_set_destroy(contact_reffed);
	tp_handle_set_destroy(room_reffed);
}

static gboolean _parse_atom(IdleParser *parser, GValueArray *arr, char atom, const gchar *token, TpHandleSet *contact_reffed, TpHandleSet *room_reffed) {
	IdleParserPrivate *priv = IDLE_PARSER_GET_PRIVATE(parser);
	TpHandle handle;
	GValue val = {0};
	TpHandleRepoIface *contact_repo = tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->conn), TP_HANDLE_TYPE_CONTACT);
	TpHandleRepoIface *room_repo = tp_base_connection_get_handles(TP_BASE_CONNECTION(priv->conn), TP_HANDLE_TYPE_ROOM);

	if (token[0] == ':')
		token++;

	IDLE_DEBUG("parsing atom \"%s\" as %c", token, atom);

	switch (atom) {
		case 'I':
			IDLE_DEBUG("ignored token");
			return TRUE;
			break;

		case 'c':
		case 'r':
		case 'C': {
			gchar *id, *bang = NULL;
			gchar modechar = '\0';

			if (idle_muc_channel_is_modechar(token[0])) {
				modechar = token[0];
				token++;
			}

			id = g_strdup(token);

			if (atom != 'r') {
				bang = strchr(id, '!');
				if (bang)
					*bang = '\0';
			}

			if (atom == 'r') {
				if ((handle = tp_handle_ensure(room_repo, id, NULL, NULL))) {
					tp_handle_set_add(room_reffed, handle);
				}
			} else {
				if ((handle = tp_handle_ensure(contact_repo, id, NULL, NULL))) {
					tp_handle_set_add(contact_reffed, handle);

					idle_connection_canon_nick_receive(priv->conn, handle, id);
				}
			}

			g_free(id);

			if (!handle)
				return FALSE;

			g_value_init(&val, G_TYPE_UINT);
			g_value_set_uint(&val, handle);
			g_value_array_append(arr, &val);
			g_value_unset(&val);

			IDLE_DEBUG("set handle %u", handle);

			if (atom == 'C') {
				g_value_init(&val, G_TYPE_CHAR);
#if GLIB_CHECK_VERSION(2, 31, 0)
				g_value_set_schar(&val, modechar);
#else
				g_value_set_char(&val, modechar);
#endif
				g_value_array_append(arr, &val);
				g_value_unset(&val);

				IDLE_DEBUG("set modechar %c", modechar);
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

				IDLE_DEBUG("set int %d", dval);

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
			IDLE_DEBUG("set string \"%s\"", token);

			return TRUE;
			break;

		default:
			IDLE_DEBUG("unknown atom %c", atom);
			return FALSE;
			break;
	}
}

