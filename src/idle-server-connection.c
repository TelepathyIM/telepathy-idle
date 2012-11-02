/*
 * This file is part of telepathy-idle
 *
 * Copyright (C) 2006-2007 Collabora Limited
 * Copyright (C) 2006-2007 Nokia Corporation
 * Copyright (C) 2011      Debarshi Ray <rishi@gnu.org>
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

#include "idle-server-connection.h"

#include <string.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <telepathy-glib/errors.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_NETWORK
#include "idle-connection.h"
#include "idle-debug.h"

typedef struct _IdleServerConnectionPrivate IdleServerConnectionPrivate;

#define IDLE_SERVER_CONNECTION_GET_PRIVATE(conn) (G_TYPE_INSTANCE_GET_PRIVATE((conn), IDLE_TYPE_SERVER_CONNECTION, IdleServerConnectionPrivate))

G_DEFINE_TYPE(IdleServerConnection, idle_server_connection, G_TYPE_OBJECT)

enum {
	STATUS_CHANGED,
	RECEIVED,
	LAST_SIGNAL
};

enum {
	PROP_HOST = 1,
	PROP_PORT
};

struct _IdleServerConnectionPrivate {
	gchar *host;
	guint16 port;

	gchar input_buffer[IRC_MSG_MAXLEN + 3];
	gchar output_buffer[IRC_MSG_MAXLEN + 2]; /* No need for a trailing '\0' */
	gsize count;
	gsize nwritten;

	guint reason;

	GSocketClient *socket_client;
	GIOStream *io_stream;
	GCancellable *cancellable;

	IdleServerConnectionState state;
};

static GObject *idle_server_connection_constructor(GType type, guint n_props, GObjectConstructParam *props);

static guint signals[LAST_SIGNAL] = {0};

static void idle_server_connection_init(IdleServerConnection *conn) {
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);

	priv->host = NULL;
	priv->port = 0;

	priv->socket_client = g_socket_client_new();

	priv->state = SERVER_CONNECTION_STATE_NOT_CONNECTED;
}

static GObject *idle_server_connection_constructor(GType type, guint n_props, GObjectConstructParam *props) {
	GObject *ret;

	ret = G_OBJECT_CLASS(idle_server_connection_parent_class)->constructor(type, n_props, props);

	return ret;
}

static void idle_server_connection_dispose(GObject *obj) {
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(obj);
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);

	if (priv->socket_client != NULL) {
		g_object_unref(priv->socket_client);
		priv->socket_client = NULL;
	}
}

static void idle_server_connection_finalize(GObject *obj) {
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(obj);
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);

	g_free(priv->host);
}

static void idle_server_connection_get_property(GObject 	*obj, guint prop_id, GValue *value, GParamSpec *pspec) {
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(obj);
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);

	switch (prop_id) {
		case PROP_HOST:
			g_value_set_string(value, priv->host);
			break;

		case PROP_PORT:
			g_value_set_uint(value, priv->port);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
			break;
	}
}

static void idle_server_connection_set_property(GObject 	*obj,
												guint 		 prop_id,
												const GValue 		*value,
												GParamSpec 	*pspec) {
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(obj);
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);

	switch (prop_id) {
		case PROP_HOST:
			g_free(priv->host);
			priv->host = g_value_dup_string(value);
			break;

		case PROP_PORT:
			priv->port = (guint16) g_value_get_uint(value);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
			break;
	}
}

static void idle_server_connection_class_init(IdleServerConnectionClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	g_type_class_add_private(klass, sizeof(IdleServerConnectionPrivate));

	object_class->constructor = idle_server_connection_constructor;
	object_class->dispose = idle_server_connection_dispose;
	object_class->finalize = idle_server_connection_finalize;

	object_class->get_property = idle_server_connection_get_property;
	object_class->set_property = idle_server_connection_set_property;

	pspec = g_param_spec_string("host", "Remote host",
								"Hostname of the remote service to connect to.",
								NULL,
								G_PARAM_READABLE|
								G_PARAM_WRITABLE|
								G_PARAM_STATIC_NICK|
								G_PARAM_STATIC_BLURB);

	g_object_class_install_property(object_class, PROP_HOST, pspec);

	pspec = g_param_spec_uint("port", "Remote port",
							  "Port number of the remote service to connect to.",
							  0, G_MAXUINT16, 0,
							  G_PARAM_READABLE|
							  G_PARAM_WRITABLE|
							  G_PARAM_STATIC_NICK|
							  G_PARAM_STATIC_BLURB);

	g_object_class_install_property(object_class, PROP_PORT, pspec);

	signals[STATUS_CHANGED] = g_signal_new("status-changed",
						G_OBJECT_CLASS_TYPE(klass),
						G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
						0,
						NULL, NULL,
						g_cclosure_marshal_generic,
						G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	signals[RECEIVED] = g_signal_new("received",
						G_OBJECT_CLASS_TYPE(klass),
						G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
						0,
						NULL, NULL,
						g_cclosure_marshal_VOID__STRING,
						G_TYPE_NONE, 1, G_TYPE_STRING);

}

static void change_state(IdleServerConnection *conn, IdleServerConnectionState state, guint reason) {
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);

	if (state == priv->state)
		return;

	IDLE_DEBUG("emitting status-changed, state %u, reason %u", state, reason);

	priv->state = state;
	g_signal_emit(conn, signals[STATUS_CHANGED], 0, state, reason);
}

static void _input_stream_read(IdleServerConnection *conn, GInputStream *input_stream, GAsyncReadyCallback callback) {
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);

	memset(priv->input_buffer, '\0', sizeof(priv->input_buffer));
	g_input_stream_read_async (input_stream, &priv->input_buffer, sizeof(priv->input_buffer) - 1, G_PRIORITY_DEFAULT, NULL, callback, conn);
}

static void _input_stream_read_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
	GInputStream *input_stream = G_INPUT_STREAM(source_object);
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(user_data);
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);
	gssize ret;
	GError *error = NULL;

	if (priv->io_stream == NULL) /* ie. we are in the process of disconnecting */
		goto cleanup;

	ret = g_input_stream_read_finish(input_stream, res, &error);

	if (ret == -1) {
		IDLE_DEBUG("g_input_stream_read failed: %s", error->message);
		g_error_free(error);
		goto disconnect;
	} else if (ret == 0) {
		IDLE_DEBUG("g_input_stream_read returned end-of-file");
		goto disconnect;
	}

	g_signal_emit(conn, signals[RECEIVED], 0, priv->input_buffer);

	_input_stream_read(conn, input_stream, _input_stream_read_ready);
	return;

disconnect:
	if (priv->state == SERVER_CONNECTION_STATE_CONNECTED)
		idle_server_connection_disconnect_full_async(conn, SERVER_CONNECTION_STATE_REASON_ERROR, NULL, NULL, NULL);
cleanup:
	g_object_unref(conn);
}

static void _connect_to_host_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
	GSocketClient *socket_client = G_SOCKET_CLIENT(source_object);
	GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT(user_data);
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(g_async_result_get_source_object(G_ASYNC_RESULT(result)));
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);
	GInputStream *input_stream;
	GSocket *socket_;
	GSocketConnection *socket_connection;
	gint nodelay = 1;
	gint socket_fd;
	GError *error = NULL;

	socket_connection = g_socket_client_connect_to_host_finish(socket_client, res, &error);
	if (socket_connection == NULL) {
		IDLE_DEBUG("g_socket_client_connect_to_host failed: %s", error->message);
		g_simple_async_result_set_error(result, TP_ERROR, TP_ERROR_NETWORK_ERROR, "%s", error->message);
		g_error_free(error);
		change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, SERVER_CONNECTION_STATE_REASON_ERROR);
		g_object_unref(conn);
		goto cleanup;
	}

	socket_ = g_socket_connection_get_socket(socket_connection);
	g_socket_set_keepalive(socket_, TRUE);

	socket_fd = g_socket_get_fd(socket_);
	setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

	g_tcp_connection_set_graceful_disconnect(G_TCP_CONNECTION(socket_connection), TRUE);

	priv->io_stream = G_IO_STREAM(socket_connection);

	input_stream = g_io_stream_get_input_stream(priv->io_stream);
	_input_stream_read(conn, input_stream, _input_stream_read_ready);
	change_state(conn, SERVER_CONNECTION_STATE_CONNECTED, SERVER_CONNECTION_STATE_REASON_REQUESTED);

cleanup:
	g_simple_async_result_complete(result);
	g_object_unref(result);
}

void idle_server_connection_connect_async(IdleServerConnection *conn, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data) {
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);
	GSimpleAsyncResult *result;

	if (priv->state != SERVER_CONNECTION_STATE_NOT_CONNECTED) {
		IDLE_DEBUG("already connecting or connected!");
		g_simple_async_report_error_in_idle(G_OBJECT(conn),
			callback, user_data,
			TP_ERROR, TP_ERROR_NOT_AVAILABLE,
			"already connecting or connected!");
		return;
	}

	if ((priv->host == NULL) || (priv->host[0] == '\0')) {
		IDLE_DEBUG("host not set!");
		g_simple_async_report_error_in_idle(G_OBJECT(conn),
			callback, user_data,
			TP_ERROR, TP_ERROR_NOT_AVAILABLE,
			"host not set!");
		return;
	}

	if (priv->port == 0) {
		IDLE_DEBUG("port not set!");
		g_simple_async_report_error_in_idle(G_OBJECT(conn),
			callback, user_data,
			TP_ERROR, TP_ERROR_NOT_AVAILABLE,
			"port not set!");
		return;
	}

	result = g_simple_async_result_new(G_OBJECT(conn), callback, user_data, idle_server_connection_connect_async);
	g_socket_client_connect_to_host_async(priv->socket_client, priv->host, priv->port, cancellable, _connect_to_host_ready, result);
	change_state(conn, SERVER_CONNECTION_STATE_CONNECTING, SERVER_CONNECTION_STATE_REASON_REQUESTED);
}

gboolean idle_server_connection_connect_finish(IdleServerConnection *conn, GAsyncResult *result, GError **error) {
	g_return_val_if_fail(g_simple_async_result_is_valid(result, G_OBJECT(conn), idle_server_connection_connect_async), FALSE);
	return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT(result), error);
}

static void _close_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
	GIOStream *io_stream = G_IO_STREAM(source_object);
	GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT(user_data);
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(g_async_result_get_source_object(G_ASYNC_RESULT(result)));
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);
	GError *error = NULL;

	change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, priv->reason);
	g_object_unref(conn);

	if (!g_io_stream_close_finish(io_stream, res, &error)) {
		IDLE_DEBUG("g_io_stream_close failed: %s", error->message);
		g_simple_async_result_set_error(result, TP_ERROR, TP_ERROR_NETWORK_ERROR, "%s", error->message);
		g_error_free(error);
	}

	g_simple_async_result_complete(result);
	g_object_unref(result);
}

void idle_server_connection_disconnect_async(IdleServerConnection *conn, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data) {
	idle_server_connection_disconnect_full_async(conn,
						     SERVER_CONNECTION_STATE_REASON_REQUESTED,
						     cancellable,
						     callback,
						     user_data);
}

void idle_server_connection_force_disconnect(IdleServerConnection *conn) {
	/* Passing a cancelled cancellable to g_io_stream_close_async() stops us
	 * waiting for a TCP-level reply from the server which we already know
	 * has gone away. Quoth the docs for g_io_stream_close():
	 *
	 *     Cancelling a close will still leave the stream closed, but some
	 *     streams can use a faster close that doesn't block to e.g. check
	 *     errors.
	 */
	GCancellable *kill_me_now = g_cancellable_new();

	g_cancellable_cancel(kill_me_now);
	idle_server_connection_disconnect_full_async(conn,
		SERVER_CONNECTION_STATE_REASON_ERROR, kill_me_now, NULL, NULL);
	g_object_unref(kill_me_now);
}

void idle_server_connection_disconnect_full_async(IdleServerConnection *conn, guint reason, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data) {
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);
	GSimpleAsyncResult *result;

	g_assert(priv != NULL);

	if (priv->state != SERVER_CONNECTION_STATE_CONNECTED) {
		IDLE_DEBUG("the connection was not open");
		g_simple_async_report_error_in_idle(G_OBJECT(conn),
			callback, user_data,
			TP_ERROR, TP_ERROR_NOT_AVAILABLE,
			"the connection was not open");
		return;
	}

	if (priv->io_stream == NULL) {
		IDLE_DEBUG("We were exploding anyway");
		g_simple_async_report_error_in_idle(G_OBJECT(conn),
			callback, user_data,
			TP_ERROR, TP_ERROR_NOT_AVAILABLE,
			"We were exploding anyway");
		return;
	}

	priv->reason = reason;

	result = g_simple_async_result_new(G_OBJECT(conn), callback, user_data, idle_server_connection_disconnect_full_async);
	g_io_stream_close_async(priv->io_stream, G_PRIORITY_DEFAULT, cancellable, _close_ready, result);
	g_object_unref(priv->io_stream);
	priv->io_stream = NULL;
}

gboolean idle_server_connection_disconnect_finish(IdleServerConnection *conn, GAsyncResult *result, GError **error) {
	g_return_val_if_fail(g_simple_async_result_is_valid(result, G_OBJECT(conn), idle_server_connection_disconnect_full_async), FALSE);
	return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT(result), error);
}

static void _write_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
	GOutputStream *output_stream = G_OUTPUT_STREAM(source_object);
	GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT(user_data);
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(g_async_result_get_source_object(G_ASYNC_RESULT(result)));
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);
	gssize nwrite;
	GError *error = NULL;

	g_object_unref(conn);

	nwrite = g_output_stream_write_finish(output_stream, res, &error);
	if (nwrite == -1) {
		IDLE_DEBUG("g_output_stream_write failed : %s", error->message);
		g_simple_async_result_set_error(result, TP_ERROR, TP_ERROR_NETWORK_ERROR, "%s", error->message);
		g_error_free(error);
		goto cleanup;
	}

	priv->nwritten += nwrite;
	if (priv->nwritten < priv->count) {
		g_output_stream_write_async(output_stream, priv->output_buffer + priv->nwritten, priv->count - priv->nwritten, G_PRIORITY_DEFAULT, priv->cancellable, _write_ready, result);
		return;
	}

cleanup:
	if (priv->cancellable != NULL) {
		g_object_unref(priv->cancellable);
		priv->cancellable = NULL;
	}
	g_simple_async_result_complete(result);
	g_object_unref(result);
}

void idle_server_connection_send_async(IdleServerConnection *conn, const gchar *cmd, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data) {
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);
	GOutputStream *output_stream;
	GSimpleAsyncResult *result;
	gsize output_buffer_size = sizeof(priv->output_buffer);

	if (priv->state != SERVER_CONNECTION_STATE_CONNECTED
            || priv->io_stream == NULL) {
		IDLE_DEBUG("connection was not open!");
		g_simple_async_report_error_in_idle(G_OBJECT(conn),
			callback, user_data,
			TP_ERROR, TP_ERROR_NOT_AVAILABLE,
			"connection was not open!");
		return;
	}

	priv->count = strlen(cmd);
	if (priv->count > output_buffer_size)
		priv->count = output_buffer_size;

	/* We only need to copy priv->count bytes, but padding the rest
         * with null bytes gives us cleaner debug messages, without
         * affecting the readability of the code.
         */
	strncpy(priv->output_buffer, cmd, output_buffer_size);

	priv->nwritten = 0;

	if (cancellable != NULL) {
		priv->cancellable = cancellable;
		g_object_ref(priv->cancellable);
	}

	output_stream = g_io_stream_get_output_stream(priv->io_stream);
	result = g_simple_async_result_new(G_OBJECT(conn), callback, user_data, idle_server_connection_send_async);
	g_output_stream_write_async(output_stream, priv->output_buffer, priv->count, G_PRIORITY_DEFAULT, cancellable, _write_ready, result);

	IDLE_DEBUG("sending \"%s\" to OutputStream %p", priv->output_buffer, output_stream);
}

gboolean idle_server_connection_send_finish(IdleServerConnection *conn, GAsyncResult *result, GError **error) {
	/* we can't check the source tag here because we use
	 * report_error_in_idle which never sets a source tag */
	g_return_val_if_fail(g_simple_async_result_is_valid(result, G_OBJECT(conn), NULL), FALSE);
	return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT(result), error);
}

IdleServerConnectionState idle_server_connection_get_state(IdleServerConnection *conn) {
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);
	return priv->state;
}

void idle_server_connection_set_tls(IdleServerConnection *conn, gboolean tls) {
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);
	g_socket_client_set_tls(priv->socket_client, tls);
	g_socket_client_set_tls_validation_flags(priv->socket_client,
		G_TLS_CERTIFICATE_VALIDATE_ALL
		& ~G_TLS_CERTIFICATE_UNKNOWN_CA
		& ~G_TLS_CERTIFICATE_BAD_IDENTITY
		& ~G_TLS_CERTIFICATE_EXPIRED);
}
