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

#ifndef __IDLE_PARSER_H__
#define __IDLE_PARSER_H__

#include <glib-object.h>

#include <telepathy-glib/handle.h>

G_BEGIN_DECLS

#define IDLE_TYPE_PARSER \
	(idle_parser_get_type())

#define IDLE_PARSER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), IDLE_TYPE_PARSER, IdleParser))

#define IDLE_PARSER_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), IDLE_TYPE_PARSER, IdleParser))

#define IDLE_IS_PARSER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_PARSER))

#define IDLE_IS_PARSER_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_PARSER))

#define IDLE_PARSER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS((obj), IDLE_TYPE_PARSER, IdleParserClass))

typedef enum {
	IDLE_PARSER_CMD_ERROR = 0,
	IDLE_PARSER_CMD_PING,

	IDLE_PARSER_LAST_NON_PREFIX_CMD = IDLE_PARSER_CMD_PING,

	IDLE_PARSER_PREFIXCMD_INVITE,
	IDLE_PARSER_PREFIXCMD_JOIN,
	IDLE_PARSER_PREFIXCMD_KICK,
	IDLE_PARSER_PREFIXCMD_MODE_CHANNEL,
	IDLE_PARSER_PREFIXCMD_MODE_USER,
	IDLE_PARSER_PREFIXCMD_NICK,
	IDLE_PARSER_PREFIXCMD_NOTICE_CHANNEL,
	IDLE_PARSER_PREFIXCMD_NOTICE_USER,
	IDLE_PARSER_PREFIXCMD_PART,
	IDLE_PARSER_PREFIXCMD_PONG,
	IDLE_PARSER_PREFIXCMD_PRIVMSG_CHANNEL,
	IDLE_PARSER_PREFIXCMD_PRIVMSG_USER,
	IDLE_PARSER_PREFIXCMD_QUIT,
	IDLE_PARSER_PREFIXCMD_TOPIC,

	IDLE_PARSER_NUMERIC_AWAY,
	IDLE_PARSER_NUMERIC_BADCHANNELKEY,
	IDLE_PARSER_NUMERIC_BANNEDFROMCHAN,
	IDLE_PARSER_NUMERIC_CANNOTSENDTOCHAN,
	IDLE_PARSER_NUMERIC_CHANNELISFULL,
	IDLE_PARSER_NUMERIC_ENDOFWHOIS,
	IDLE_PARSER_NUMERIC_ERRONEOUSNICKNAME,
	IDLE_PARSER_NUMERIC_INVITEONLYCHAN,
	IDLE_PARSER_NUMERIC_MODEREPLY,
	IDLE_PARSER_NUMERIC_NAMEREPLY,
	IDLE_PARSER_NUMERIC_NAMEREPLY_END,
	IDLE_PARSER_NUMERIC_NICKNAMEINUSE,
	IDLE_PARSER_NUMERIC_NOSUCHNICK,
	IDLE_PARSER_NUMERIC_NOSUCHSERVER,
	IDLE_PARSER_NUMERIC_NOWAWAY,
	IDLE_PARSER_NUMERIC_TOPIC,
	IDLE_PARSER_NUMERIC_TOPIC_STAMP,
	IDLE_PARSER_NUMERIC_TRYAGAIN,
	IDLE_PARSER_NUMERIC_UNAWAY,
	IDLE_PARSER_NUMERIC_WELCOME,
	IDLE_PARSER_NUMERIC_WHOISCHANNELS,
	IDLE_PARSER_NUMERIC_WHOISHOST,
	IDLE_PARSER_NUMERIC_WHOISLOGGEDIN,
	IDLE_PARSER_NUMERIC_WHOISOPERATOR,
	IDLE_PARSER_NUMERIC_WHOISREGNICK,
	IDLE_PARSER_NUMERIC_WHOISSECURE,
	IDLE_PARSER_NUMERIC_WHOISSERVER,
	IDLE_PARSER_NUMERIC_WHOISUSER,
	IDLE_PARSER_NUMERIC_WHOISIDLE,
	IDLE_PARSER_NUMERIC_LIST,
	IDLE_PARSER_NUMERIC_LISTEND,
	IDLE_PARSER_NUMERIC_UNKNOWNCOMMAND,

	IDLE_PARSER_LAST_MESSAGE_CODE
} IdleParserMessageCode;

typedef enum {
	IDLE_PARSER_HANDLER_RESULT_HANDLED,
	IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED,
	IDLE_PARSER_HANDLER_RESULT_NO_MORE_PLEASE
} IdleParserHandlerResult;

typedef enum {
	IDLE_PARSER_HANDLER_PRIORITY_FIRST = 0,
	IDLE_PARSER_HANDLER_PRIORITY_DEFAULT = 300,
	IDLE_PARSER_HANDLER_PRIORITY_LAST = 600,
	IDLE_PARSER_HANDLER_PRIORITY_UNHANDLED = 1000
} IdleParserHandlerPriority;

typedef struct _IdleParser IdleParser;
typedef struct _IdleParserClass IdleParserClass;

struct _IdleParser {
	GObject parent;
};

struct _IdleParserClass {
	GObjectClass parent;
};

typedef IdleParserHandlerResult (*IdleParserMessageHandler)(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data);

GType idle_parser_get_type(void);

void idle_parser_receive(IdleParser *parser, const gchar *raw_msg);
void idle_parser_add_handler(IdleParser *parser, IdleParserMessageCode code, IdleParserMessageHandler handler, gpointer user_data);
void idle_parser_add_handler_with_priority(IdleParser *parser, IdleParserMessageCode code, IdleParserMessageHandler handler, gpointer user_data, IdleParserHandlerPriority priority);
void idle_parser_remove_handlers_by_data(IdleParser *parser, gpointer user_data);

G_END_DECLS

#endif

