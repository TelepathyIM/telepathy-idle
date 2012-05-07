/*
 * This file is part of telepathy-idle
 *
 * Copyright (C) 2011 Debarshi Ray <rishi@gnu.org>
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

#include "idle-contact-info.h"

#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/handle-repo.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_CONNECTION
#include "idle-debug.h"
#include "idle-muc-channel.h"
#include "idle-parser.h"

typedef struct _ContactInfoRequest ContactInfoRequest;

struct _ContactInfoRequest {
	guint handle;
	const gchar *nick;
	gboolean is_away;
	gboolean is_operator;
	gboolean is_reg_nick;
	gboolean is_secure;
	GPtrArray *contact_info;
	DBusGMethodInvocation *context;
};

/*
 * _insert_contact_field:
 * @contact_info: an array of Contact_Info_Field structures
 * @field_name: a vCard field name in any case combination
 * @field_params: a list of vCard type-parameters, typically of the form
 *  type=xxx; must be in lower-case if case-insensitive
 * @field_values: for unstructured fields, an array containing one element;
 *  for structured fields, the elements of the field in order
 */
static void _insert_contact_field(GPtrArray *contact_info, const gchar *field_name, const gchar * const *field_params, const gchar * const *field_values) {
	g_ptr_array_add (contact_info, tp_value_array_build (3,
		G_TYPE_STRING, field_name,
		G_TYPE_STRV, field_params,
		G_TYPE_STRV, field_values,
		G_TYPE_INVALID));
}

static ContactInfoRequest * _get_matching_request(IdleConnection *conn, GValueArray *args) {
	ContactInfoRequest *request;
	TpHandle handle = g_value_get_uint(g_value_array_get_nth(args, 0));

	if (g_queue_is_empty(conn->contact_info_requests))
		return NULL;

	request = g_queue_peek_head(conn->contact_info_requests);
	if (request->handle != handle)
		return NULL;

	if (request->contact_info == NULL)
		request->contact_info = dbus_g_type_specialized_construct(TP_ARRAY_TYPE_CONTACT_INFO_FIELD_LIST);

	return request;
}

static void _send_request_contact_info(IdleConnection *conn, ContactInfoRequest *request) {
	gchar cmd[IRC_MSG_MAXLEN + 1];

	g_snprintf(cmd, IRC_MSG_MAXLEN + 1, "WHOIS %s %s", request->nick, request->nick);
	idle_connection_send(conn, cmd);
}

static void _dequeue_request_contact_info(IdleConnection *conn) {
	ContactInfoRequest *request = g_queue_pop_head(conn->contact_info_requests);

	if (request->contact_info != NULL)
		g_boxed_free(TP_ARRAY_TYPE_CONTACT_INFO_FIELD_LIST, request->contact_info);

	g_slice_free(ContactInfoRequest, request);

	if (g_queue_is_empty(conn->contact_info_requests))
		return;

	request = g_queue_peek_head(conn->contact_info_requests);
	_send_request_contact_info(conn, request);
}

static void _queue_request_contact_info(IdleConnection *conn, guint handle, const gchar *nick, DBusGMethodInvocation *context) {
	ContactInfoRequest *request;

	request = g_slice_new0(ContactInfoRequest);
	request->handle = handle;
	request->nick = nick;
	request->is_away = FALSE;
	request->is_operator = FALSE;
	request->is_reg_nick = FALSE;
	request->is_secure = FALSE;
	request->contact_info = NULL;
	request->context = context;

	if (g_queue_is_empty(conn->contact_info_requests))
		_send_request_contact_info(conn, request);

	g_queue_push_tail(conn->contact_info_requests, request);
}

static void _return_from_request_contact_info(IdleConnection *conn) {
	ContactInfoRequest *request = g_queue_peek_head(conn->contact_info_requests);

	tp_svc_connection_interface_contact_info_return_from_request_contact_info(request->context, request->contact_info);
	tp_svc_connection_interface_contact_info_emit_contact_info_changed(conn, request->handle, request->contact_info);
	_dequeue_request_contact_info(conn);
}

static void idle_connection_request_contact_info(TpSvcConnectionInterfaceContactInfo *iface, guint contact, DBusGMethodInvocation *context) {
	IdleConnection *self = IDLE_CONNECTION(iface);
	TpBaseConnection *base = TP_BASE_CONNECTION(self);
	TpHandleRepoIface *contact_handles = tp_base_connection_get_handles(base, TP_HANDLE_TYPE_CONTACT);
	const gchar *nick;
	GError *error = NULL;

	TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED(base, context);

	if (!tp_handle_is_valid(contact_handles, contact, &error)) {
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return;
	}

	nick = tp_handle_inspect(contact_handles, contact);

	IDLE_DEBUG ("Queued contact info request for handle: %u (%s)", contact, nick);
	_queue_request_contact_info(self, contact, nick, context);
}

static IdleParserHandlerResult _away_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	ContactInfoRequest *request = _get_matching_request(conn, args);
	const gchar *msg;
	const gchar *field_values[2] = {NULL, NULL};

	if (request == NULL)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	field_values[0] = g_strdup_printf("%d", TP_CONNECTION_PRESENCE_TYPE_AWAY);
	_insert_contact_field(request->contact_info, "x-presence-type", NULL, field_values);
	g_free((gpointer) field_values[0]);

	field_values[0] = "away";
	_insert_contact_field(request->contact_info, "x-presence-status-identifier", NULL, field_values);

	msg = g_value_get_string(g_value_array_get_nth(args, 1));
	field_values[0] = msg;
	_insert_contact_field(request->contact_info, "x-presence-status-message", NULL, field_values);

	request->is_away = TRUE;

	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _end_of_whois_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	ContactInfoRequest *request = _get_matching_request(conn, args);
	const gchar *field_values[2] = {NULL, NULL};

	if (request == NULL)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	if (request->is_away == FALSE) {
		field_values[0] = g_strdup_printf("%d", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE);
		_insert_contact_field(request->contact_info, "x-presence-type", NULL, field_values);
		g_free((gpointer) field_values[0]);

		field_values[0] = "available";
		_insert_contact_field(request->contact_info, "x-presence-status-identifier", NULL, field_values);

		field_values[0] = "";
		_insert_contact_field(request->contact_info, "x-presence-status-message", NULL, field_values);
	}

	field_values[0] = (request->is_operator) ? "true" : "false";
	_insert_contact_field(request->contact_info, "x-irc-operator", NULL, field_values);

	field_values[0] = (request->is_reg_nick) ? "true" : "false";
	_insert_contact_field(request->contact_info, "x-irc-registered-nick", NULL, field_values);

	field_values[0] = (request->is_secure) ? "true" : "false";
	_insert_contact_field(request->contact_info, "x-irc-secure-connection", NULL, field_values);

	_return_from_request_contact_info(conn);
	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _no_such_server_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	TpBaseConnection *base = TP_BASE_CONNECTION(conn);
	TpHandleRepoIface *contact_handles = tp_base_connection_get_handles(base, TP_HANDLE_TYPE_CONTACT);
	TpHandle handle;
	ContactInfoRequest *request;
	GValueArray *norm_args = g_value_array_copy(args);
	GValue value = {0};
	const gchar *server;
	GError *error = NULL;

	/* We issue our requests as WHOIS <nick> <nick>, where the first <nick> is actually a shorthand for the server to which it is connected to.
	 * Therefore, if this error was caused by us then the value of <server name> in the error message will be <nick>. Otherwise, this error was not
	 * caused by us, and we do not want to handle it.
	 *
	 * To check this we map the value of the <server name> to a handle and see if it matches the handle for which we had made the request.
	 */

	server = g_value_get_string(g_value_array_get_nth(args, 0));
	handle = tp_handle_ensure(contact_handles, server, NULL, NULL);

	g_value_array_remove(norm_args, 0);

	g_value_init(&value, G_TYPE_UINT);
	g_value_set_uint(&value, handle);
	g_value_array_prepend(norm_args, &value);

	request = _get_matching_request(conn, norm_args);
	if (request == NULL)
		goto cleanup;

	error = g_error_new(TP_ERROR, TP_ERROR_DOES_NOT_EXIST, "User '%s' unknown; they may have disconnected", server);
	dbus_g_method_return_error(request->context, error);
	g_error_free(error);

	_dequeue_request_contact_info(conn);

cleanup:
	g_value_array_free(norm_args);
	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _try_again_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	ContactInfoRequest *request;
	const gchar *command;
	const gchar *msg;
	GError *error = NULL;

	/* The RPL_TRYAGAIN message does not contain the nick for which the request was issued, but only the type of the message, which in this case is
	 * WHOIS. So we assume that the last WHOIS request that we had issued has resulted in this RPL_TRYAGAIN. This is fine as long nobody else is
	 * issuing a WHOIS because we issue a new request only after the earlier one was successfully completed or resulted in an error.
	 */

	if (g_queue_is_empty(conn->contact_info_requests))
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	command = g_value_get_string(g_value_array_get_nth(args, 0));
	if (g_ascii_strcasecmp(command, "WHOIS"))
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	request = g_queue_peek_head(conn->contact_info_requests);

	msg = g_value_get_string(g_value_array_get_nth(args, 1));

	error = g_error_new_literal(TP_ERROR, TP_ERROR_SERVICE_BUSY, msg);
	dbus_g_method_return_error(request->context, error);
	g_error_free(error);

	_dequeue_request_contact_info(conn);

	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _whois_channels_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	ContactInfoRequest *request = _get_matching_request(conn, args);
	gchar *channels;
	gchar **channelsv;
	const gchar *field_values[2] = {NULL, NULL};
	guint i;

	if (request == NULL)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	if (args->n_values != 2)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	channels = g_value_dup_string(g_value_array_get_nth(args, 1));
	g_strchomp(channels);
	channelsv = g_strsplit(channels, " ", -1);

	for (i = 0; channelsv[i] != NULL; i++) {
		const gchar *channel = channelsv[i];
		gchar *field_params[2] = {NULL, NULL};

		if (idle_muc_channel_is_modechar(channel[0]) && idle_muc_channel_is_typechar(channel[1])) {
			field_params[0] = g_strdup_printf("role=%c", channel[0]);
			channel++;
		}

		field_values[0] = channel;
		_insert_contact_field(request->contact_info, "x-irc-channel", (const gchar **) field_params, field_values);
		g_free(field_params[0]);
	}

	g_strfreev(channelsv);
	g_free(channels);

	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _whois_host_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	ContactInfoRequest *request = _get_matching_request(conn, args);
	gchar *msg;
	gchar **msgv;

	if (request == NULL)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	msg = g_value_dup_string(g_value_array_get_nth(args, 1));
	g_strchomp(msg);

	if (!g_str_has_prefix(msg, "is connecting from "))
		goto cleanup;

	msgv = g_strsplit(msg, " ", -1);

	/* msg == "is connecting from *@<hostname> <IP>" */
	_insert_contact_field(request->contact_info, "x-host", NULL, (const gchar **) (msgv + 3));

	g_strfreev(msgv);

cleanup:
	g_free(msg);
	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _whois_idle_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	ContactInfoRequest *request = _get_matching_request(conn, args);
	guint sec;
	const gchar *field_values[2] = {NULL, NULL};

	if (request == NULL)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	sec = g_value_get_uint(g_value_array_get_nth(args, 1));

	field_values[0] = g_strdup_printf("%u", sec);
	_insert_contact_field(request->contact_info, "x-idle-time", NULL, field_values);
	g_free((gpointer) field_values[0]);

	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _whois_logged_in_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	ContactInfoRequest *request = _get_matching_request(conn, args);
	const gchar *msg;
	const gchar *nick;
	const gchar *field_values[2] = {NULL, NULL};

	if (request == NULL)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	msg = g_value_get_string(g_value_array_get_nth(args, 2));
	if (g_strcmp0(msg, "is logged in as"))
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	nick = g_value_get_string(g_value_array_get_nth(args, 1));
	field_values[0] = nick;
	_insert_contact_field(request->contact_info, "nickname", NULL, field_values);

	request->is_reg_nick = TRUE;

	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _whois_operator_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	ContactInfoRequest *request = _get_matching_request(conn, args);

	if (request == NULL)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	request->is_operator = TRUE;

	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _whois_reg_nick_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	ContactInfoRequest *request = _get_matching_request(conn, args);

	if (request == NULL)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	request->is_reg_nick = TRUE;

	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _whois_secure_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	ContactInfoRequest *request = _get_matching_request(conn, args);

	if (request == NULL)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	request->is_secure = TRUE;

	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _whois_server_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	ContactInfoRequest *request = _get_matching_request(conn, args);
	const gchar *server;
	const gchar *server_info;
	const gchar *field_values[3] = {NULL, NULL, NULL};

	if (request == NULL)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	server = g_value_get_string(g_value_array_get_nth(args, 1));
	server_info = g_value_get_string(g_value_array_get_nth(args, 2));
	field_values[0] = server;
	field_values[1] = server_info;
	_insert_contact_field(request->contact_info, "x-irc-server", NULL, field_values);

	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static IdleParserHandlerResult _whois_user_handler(IdleParser *parser, IdleParserMessageCode code, GValueArray *args, gpointer user_data) {
	IdleConnection *conn = IDLE_CONNECTION(user_data);
	ContactInfoRequest *request = _get_matching_request(conn, args);
	const gchar *name;
	const gchar *field_values[2] = {NULL, NULL};

	if (request == NULL)
		return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;

	name = g_value_get_string(g_value_array_get_nth(args, 3));
	field_values[0] = name;
	_insert_contact_field(request->contact_info, "fn", NULL, field_values);

	return IDLE_PARSER_HANDLER_RESULT_NOT_HANDLED;
}

static void idle_contact_info_properties_getter(GObject *object, GQuark interface, GQuark name, GValue *value, gpointer getter_data) {
	GQuark q_supported_fields = g_quark_from_static_string("SupportedFields");

	if (name == q_supported_fields) {
		GPtrArray *fields = dbus_g_type_specialized_construct(TP_ARRAY_TYPE_FIELD_SPECS);

		g_value_set_boxed(value, fields);
		g_boxed_free(TP_ARRAY_TYPE_FIELD_SPECS, fields);
	}
	else /* ContactInfoFlags */
		g_value_set_uint(value, 0);
}

static void _contact_info_requests_foreach_free(gpointer data, gpointer user_data) {
	g_slice_free(ContactInfoRequest, data);
}

void idle_contact_info_finalize (GObject *object) {
	IdleConnection *conn = IDLE_CONNECTION(object);

	g_queue_foreach(conn->contact_info_requests, _contact_info_requests_foreach_free, NULL);
	g_queue_free(conn->contact_info_requests);
}

void idle_contact_info_class_init (IdleConnectionClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	static TpDBusPropertiesMixinPropImpl props[] = {
		{"ContactInfoFlags", NULL, NULL},
		{"SupportedFields", NULL, NULL},
		{NULL}
	};

	tp_dbus_properties_mixin_implement_interface(object_class,
		TP_IFACE_QUARK_CONNECTION_INTERFACE_CONTACT_INFO,
		idle_contact_info_properties_getter,
		NULL,
		props);
}

static void
idle_contact_info_fill_contact_attributes (
    GObject *obj,
    const GArray *contacts,
    GHashTable *attributes_hash)
{
  /* We don't cache contact info, so we just never put /info into the
   * attributes hash. This is spec-compliant: we don't implement
   * GetContactInfo, and the spec says the attribute should be the same as the
   * value returned by that method (or omitted if unknown). This function
   * exists at all to make ContactInfo show up in ContactAttributeInterfaces
   * (otherwise tp-glib might be justified in falling back to
   * GetContactInfo(), which we know will fail).
   */
}

void idle_contact_info_init (IdleConnection *conn) {
	conn->contact_info_requests = g_queue_new();

	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_WHOISUSER, _whois_user_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_WHOISCHANNELS, _whois_channels_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_WHOISSERVER, _whois_server_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_WHOISOPERATOR, _whois_operator_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_AWAY, _away_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_WHOISHOST, _whois_host_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_WHOISREGNICK, _whois_reg_nick_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_WHOISSECURE, _whois_secure_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_WHOISIDLE, _whois_idle_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_WHOISLOGGEDIN, _whois_logged_in_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_ENDOFWHOIS, _end_of_whois_handler, conn);

	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_NOSUCHSERVER, _no_such_server_handler, conn);
	idle_parser_add_handler(conn->parser, IDLE_PARSER_NUMERIC_TRYAGAIN, _try_again_handler, conn);

	tp_contacts_mixin_add_contact_attributes_iface ((GObject *) conn,
		TP_IFACE_CONNECTION_INTERFACE_CONTACT_INFO,
		idle_contact_info_fill_contact_attributes);
}

void idle_contact_info_iface_init(gpointer g_iface, gpointer iface_data) {
	TpSvcConnectionInterfaceContactInfoClass *klass = (TpSvcConnectionInterfaceContactInfoClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_contact_info_implement_##x (\
		klass, idle_connection_##x)
	IMPLEMENT(request_contact_info);
#undef IMPLEMENT
}
