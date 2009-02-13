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

#include "idle-server-connection.h"

#include <telepathy-glib/errors.h>

#include <errno.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_NETWORK
#include "idle-connection.h"
#include "idle-dns-resolver.h"
#include "idle-debug.h"
#include "idle-server-connection-iface.h"

static void _server_connection_iface_init(gpointer, gpointer);
typedef struct _IdleServerConnectionPrivate IdleServerConnectionPrivate;

#define IDLE_SERVER_CONNECTION_GET_PRIVATE(conn) (G_TYPE_INSTANCE_GET_PRIVATE((conn), IDLE_TYPE_SERVER_CONNECTION, IdleServerConnectionPrivate))

G_DEFINE_TYPE_WITH_CODE(IdleServerConnection, idle_server_connection, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(IDLE_TYPE_SERVER_CONNECTION_IFACE, _server_connection_iface_init));

enum {
	PROP_HOST = 1,
	PROP_PORT
};

struct _AsyncConnectData {
	guint watch_id;

	GIOChannel *io_chan;

	IdleDNSResult *res;
	IdleDNSResult *cur;
};

#define async_connect_data_new() \
	(g_slice_new(struct _AsyncConnectData))
#define async_connect_data_new0() \
	(g_slice_new0(struct _AsyncConnectData))

static void async_connect_data_destroy(struct _AsyncConnectData *data) {
	if (data->watch_id) {
		g_source_remove(data->watch_id);
		data->watch_id = 0;
	}

	if (data->io_chan) {
		g_io_channel_shutdown(data->io_chan, FALSE, NULL);
		g_io_channel_unref(data->io_chan);
		data->io_chan = NULL;
	}

	if (data->res) {
		idle_dns_result_destroy(data->res);
		data->res = NULL;
	}

	g_slice_free(struct _AsyncConnectData, data);
}

struct _IdleServerConnectionPrivate {
	gchar *host;
	guint port;

	GIOChannel *io_chan;
	IdleServerConnectionState state;

	guint read_watch_id;

	gboolean dispose_has_run;

	IdleDNSResolver *resolver;
	struct _AsyncConnectData *connect_data;
};

static GObject *idle_server_connection_constructor(GType type, guint n_props, GObjectConstructParam *props);

static void idle_server_connection_init(IdleServerConnection *conn) {
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);

	priv->host = NULL;
	priv->port = 0;

	priv->io_chan = NULL;
	priv->state = SERVER_CONNECTION_STATE_NOT_CONNECTED;
	priv->read_watch_id = 0;
	priv->connect_data = NULL;
	priv->resolver = idle_dns_resolver_new();

	priv->dispose_has_run = FALSE;
}

static GObject *idle_server_connection_constructor(GType type, guint n_props, GObjectConstructParam *props) {
	GObject *ret;

	ret = G_OBJECT_CLASS(idle_server_connection_parent_class)->constructor(type, n_props, props);

	return ret;
}

static void idle_server_connection_dispose(GObject *obj) {
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(obj);
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);

	if (priv->dispose_has_run) {
		return;
	}

	IDLE_DEBUG("dispose called");
	priv->dispose_has_run = TRUE;

	if (priv->state == SERVER_CONNECTION_STATE_CONNECTED) {
		GError *error = NULL;
		g_warning("%s: connection was open when the object was deleted, it'll probably crash now...", G_STRFUNC);

		if (!idle_server_connection_iface_disconnect(IDLE_SERVER_CONNECTION_IFACE(obj), &error)){
			g_error_free(error);
		}
	}

	if (priv->connect_data != NULL) {
		async_connect_data_destroy(priv->connect_data);
		priv->connect_data = NULL;
	}

	if (priv->resolver != NULL) {
		idle_dns_resolver_destroy(priv->resolver);
		priv->resolver = NULL;
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
			priv->port = g_value_get_uint(value);
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
							  0, 0xffff, 0,
							  G_PARAM_READABLE|
							  G_PARAM_WRITABLE|
							  G_PARAM_STATIC_NICK|
							  G_PARAM_STATIC_BLURB);

	g_object_class_install_property(object_class, PROP_PORT, pspec);
}

static void change_state(IdleServerConnection *conn, IdleServerConnectionState state, guint reason) {
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);

	if (state == priv->state)
		return;

	IDLE_DEBUG("emitting status-changed, state %u, reason %u", state, reason);

	priv->state = state;
	g_signal_emit_by_name(conn, "status-changed", state, reason);
}

static gboolean iface_disconnect_impl_full(IdleServerConnectionIface *iface, GError **error, guint reason);

static gboolean io_err_cleanup_func(gpointer data) {
	GError *error = NULL;

	if (!iface_disconnect_impl_full(IDLE_SERVER_CONNECTION_IFACE(data), &error, SERVER_CONNECTION_STATE_REASON_ERROR)) {
		IDLE_DEBUG("disconnect: %s", error->message);
		g_error_free(error);
	}

	return FALSE;
}

static gboolean io_func(GIOChannel *src, GIOCondition cond, gpointer data) {
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(data);
	gchar buf[IRC_MSG_MAXLEN + 3];
	GIOStatus status;
	gsize len;
	GError *error = NULL;

	if (cond & (G_IO_ERR | G_IO_HUP)) {
		IDLE_DEBUG("got G_IO_ERR | G_IO_HUP");
		g_idle_add(io_err_cleanup_func, data);
		return FALSE;
	}

	memset(buf, 0, IRC_MSG_MAXLEN + 3);
	status = g_io_channel_read_chars(src, buf, IRC_MSG_MAXLEN + 2, &len, &error);

	if ((status != G_IO_STATUS_NORMAL) && (status != G_IO_STATUS_AGAIN)) {
		IDLE_DEBUG("status: %u, error: %s", status, (error != NULL) ? (error->message != NULL) ? error->message : "(null)" : "(null)");

		if (error) {
			g_error_free(error);
		}

		g_idle_add(io_err_cleanup_func, data);
		return FALSE;
	}

	g_signal_emit_by_name(conn, "received", buf);

	return TRUE;
}

static gboolean do_connect(IdleServerConnection *conn);

static gboolean iface_connect_impl(IdleServerConnectionIface *iface, GError **error) {
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(iface);
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);

	if (priv->state != SERVER_CONNECTION_STATE_NOT_CONNECTED) {
		IDLE_DEBUG("already connecting or connected!");
		g_set_error(error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "already connecting or connected!");
		return FALSE;
	}

	if ((priv->host == NULL) || (priv->host[0] == '\0')) {
		IDLE_DEBUG("host not set!");
		g_set_error(error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "host not set!");
		return FALSE;
	}

	if (priv->port == 0) {
		IDLE_DEBUG("port not set!");
		g_set_error(error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "port not set!");
		return FALSE;
	}

	if (!do_connect(conn)) {
		IDLE_DEBUG("do_connect failed");
		g_set_error(error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, "failed to connect");
		return FALSE;
	}

	change_state(conn, SERVER_CONNECTION_STATE_CONNECTING, SERVER_CONNECTION_STATE_REASON_REQUESTED);

	return TRUE;
}

static gboolean connect_io_func(GIOChannel *src, GIOCondition cond, gpointer data) {
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(data);
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);
	int rc;
	int fd = -1;
	int optval;
	socklen_t optlen = sizeof(optval);
	GIOChannel *io_chan;
	struct _AsyncConnectData *connect_data = priv->connect_data;

	IdleDNSResult *cur = connect_data->cur;
	IdleDNSResult *next = cur->ai_next;

	fd = g_io_channel_unix_get_fd(connect_data->io_chan);
	g_assert(getsockopt(fd, SOL_SOCKET, SO_ERROR, &optval, &optlen) == 0);

	if (optval == 0) {
		int opt;

		IDLE_DEBUG("connected!");

		change_state(conn, SERVER_CONNECTION_STATE_CONNECTED, SERVER_CONNECTION_STATE_REASON_REQUESTED);

		g_assert(priv->io_chan == NULL);
		g_assert(priv->read_watch_id == 0);

		priv->read_watch_id = g_io_add_watch(connect_data->io_chan, G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP, io_func, data);
		priv->io_chan = connect_data->io_chan;

		connect_data->io_chan = NULL;

		async_connect_data_destroy(priv->connect_data);
		priv->connect_data = NULL;

		opt = 1;

		if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
			g_warning("%s: failed to set TCP_NODELAY", G_STRFUNC);
		}

		opt = 1;

		if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
			g_warning("%s: failed to set SO_KEEPALIVE", G_STRFUNC);
		}

		return FALSE;
	}

	IDLE_DEBUG("connection failed");

	if (next == NULL) {
		IDLE_DEBUG("and this was the last address we can try, tough luck.");
		change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, SERVER_CONNECTION_STATE_REASON_ERROR);

		return FALSE;
	}

	if ((next->ai_family == cur->ai_family) &&
			(next->ai_socktype == cur->ai_socktype) &&
			(next->ai_protocol == cur->ai_protocol)) {
		IDLE_DEBUG("re-using existing socket for trying again");

		errno = 0;
		connect(fd, next->ai_addr, next->ai_addrlen);

		for (int i = 0; i < 5 && errno == ECONNABORTED; i++) {
			IDLE_DEBUG("got ECONNABORTED for %ith time", i + 1);
			errno = 0;
			connect(fd, next->ai_addr, next->ai_addrlen);
		}

		if (errno != EINPROGRESS) {
			IDLE_DEBUG("connect() failed: %s", g_strerror(errno));
			change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, SERVER_CONNECTION_STATE_REASON_ERROR);
			return FALSE;
		}

		connect_data->cur = next;

		return TRUE;
	}

	IDLE_DEBUG("we'll have to create a new socket since the address family/socket type/protocol is different");

	for (cur = next; cur != NULL; cur = cur->ai_next) {
		fd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);

		if (fd == -1) {
			if ((errno == EINVAL) || (errno == EAFNOSUPPORT)) {
				continue;
			}

			IDLE_DEBUG("socket() failed: %s", g_strerror(errno));
			return FALSE;
		}
		else {
			break;
		}
	}

	if (fd == -1) {
		IDLE_DEBUG("could not socket(): %s", g_strerror(errno));
		change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, SERVER_CONNECTION_STATE_REASON_ERROR);
		return FALSE;
	}

	io_chan = g_io_channel_unix_new(fd);
	g_io_channel_set_encoding(io_chan, NULL, NULL);
	g_io_channel_set_buffered(io_chan, FALSE);

	rc = fcntl(fd, F_SETFL, O_NONBLOCK);

	if (rc != 0) {
		IDLE_DEBUG("failed to set socket to non-blocking mode: %s", g_strerror(errno));
		g_io_channel_shutdown(io_chan, FALSE, NULL);
		g_io_channel_unref(io_chan);
		change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, SERVER_CONNECTION_STATE_REASON_ERROR);
		return FALSE;
	}

	errno = 0;
	connect(fd, cur->ai_addr, cur->ai_addrlen);

	if (errno != EINPROGRESS) {
		IDLE_DEBUG("initial connect() failed: %s", g_strerror(errno));
		g_io_channel_shutdown(io_chan, FALSE, NULL);
		g_io_channel_unref(io_chan);
		change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, SERVER_CONNECTION_STATE_REASON_ERROR);
		return FALSE;
	}

	g_io_channel_shutdown(connect_data->io_chan, FALSE, NULL);
	g_io_channel_unref(connect_data->io_chan);
	connect_data->io_chan = io_chan;

	connect_data->watch_id = g_io_add_watch(io_chan, G_IO_OUT | G_IO_ERR, connect_io_func, conn);

	return FALSE;
}

static void dns_result_callback(guint unused, IdleDNSResult *results, gpointer user_data) {
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(user_data);
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);
	IdleDNSResult *cur;
	int fd = -1;
	int rc = -1;
	GIOChannel *io_chan;

	if (!results) {
	  IDLE_DEBUG("no DNS results received");
	  change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, SERVER_CONNECTION_STATE_REASON_ERROR);
	  return;
	}

	for (cur = results; cur != NULL; cur = cur->ai_next) {
		fd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);

		if (fd == -1) {
			if ((errno == EINVAL) || (errno = EAFNOSUPPORT))
				continue;

			IDLE_DEBUG("socket() failed: %s", g_strerror(errno));
			return;
		} else {
			break;
		}
	}

	if (fd == -1) {
		IDLE_DEBUG("failed: %s", g_strerror(errno));
		change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, SERVER_CONNECTION_STATE_REASON_ERROR);
		return;
	}

	rc = fcntl(fd, F_SETFL, O_NONBLOCK);

	if (rc != 0) {
		IDLE_DEBUG("failed to set socket to non-blocking mode: %s", g_strerror(errno));
		close(fd);
		change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, SERVER_CONNECTION_STATE_REASON_ERROR);
		return;
	}

	rc = connect(fd, cur->ai_addr, cur->ai_addrlen);

	g_assert(rc == -1);

	if (errno != EINPROGRESS) {
		IDLE_DEBUG("initial connect() failed: %s", g_strerror(errno));
		close(fd);
		change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, SERVER_CONNECTION_STATE_REASON_ERROR);
		return;
	}

	if (priv->connect_data != NULL) {
		async_connect_data_destroy(priv->connect_data);
	}

	priv->connect_data = async_connect_data_new();

	io_chan = g_io_channel_unix_new(fd);
	g_io_channel_set_encoding(io_chan, NULL, NULL);
	g_io_channel_set_buffered(io_chan, FALSE);

	priv->connect_data->io_chan = io_chan;
	priv->connect_data->res = results;
	priv->connect_data->cur = cur;
	priv->connect_data->watch_id = g_io_add_watch(io_chan, G_IO_OUT | G_IO_ERR, connect_io_func, conn);
}

static gboolean do_connect(IdleServerConnection *conn) {
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);

	idle_dns_resolver_query(priv->resolver, priv->host, priv->port, dns_result_callback, conn);

	return TRUE;
}

static gboolean iface_disconnect_impl(IdleServerConnectionIface *iface, GError **error) {
	return iface_disconnect_impl_full(iface, error, SERVER_CONNECTION_STATE_REASON_REQUESTED);
}

static gboolean iface_disconnect_impl_full(IdleServerConnectionIface *iface, GError **error, guint reason) {
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(iface);
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);
	GError *io_error = NULL;
	GIOStatus status;

	g_assert(priv != NULL);

	if (priv->state != SERVER_CONNECTION_STATE_CONNECTED) {
		IDLE_DEBUG("the connection was not open");
		g_set_error(error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "the connection was not open");
		return FALSE;
	}

	if (priv->read_watch_id != 0) {
		g_source_remove(priv->read_watch_id);
		priv->read_watch_id = 0;
	}

	if (priv->io_chan != NULL) {
		status = g_io_channel_shutdown(priv->io_chan, TRUE, &io_error);

		if (status != G_IO_STATUS_NORMAL && io_error) {
			IDLE_DEBUG("g_io_channel_shutdown failed: %s", io_error->message);
			g_error_free(io_error);
		}

		g_io_channel_unref(priv->io_chan);
		priv->io_chan = NULL;
	}

	change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, reason);

	return TRUE;
}

static gboolean iface_send_impl(IdleServerConnectionIface *iface, const gchar *cmd, GError **error) {
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(iface);
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);
	GIOStatus status;
	gsize written;
	GError *local_error = NULL;

	if (priv->state != SERVER_CONNECTION_STATE_CONNECTED) {
		IDLE_DEBUG("connection was not open!");

		g_set_error(error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "connection was not open!");

		return FALSE;
	}

	status = g_io_channel_write_chars(priv->io_chan,
									  cmd,
									  -1,
									  &written,
									  &local_error);

	if (local_error) {
		IDLE_DEBUG("error: %s", local_error->message);
		g_error_free(local_error);
	}

	switch (status) {
		case G_IO_STATUS_ERROR:
			IDLE_DEBUG("got G_IO_STATUS_ERROR");

			iface_disconnect_impl_full(IDLE_SERVER_CONNECTION_IFACE(conn), NULL, SERVER_CONNECTION_STATE_REASON_ERROR);

			g_set_error(error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, "got G_IO_STATUS_ERROR");

			return FALSE;

		case G_IO_STATUS_NORMAL:
			IDLE_DEBUG("sent \"%s\"", cmd);

			return TRUE;

		case G_IO_STATUS_EOF:
			IDLE_DEBUG("got G_IO_STATUS_EOF");

			if (iface_disconnect_impl_full(IDLE_SERVER_CONNECTION_IFACE(conn), &local_error, SERVER_CONNECTION_STATE_REASON_ERROR))
				g_error_free(local_error);

			g_set_error(error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, "got G_IO_STATUS_EOF");

			return FALSE;

		case G_IO_STATUS_AGAIN:
			IDLE_DEBUG("got G_IO_STATUS_AGAIN");

			g_set_error(error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "got G_IO_STATUS_AGAIN");

			return FALSE;

		default:
			g_assert_not_reached();
			return FALSE;
	}
}

static IdleServerConnectionState iface_get_state_impl(IdleServerConnectionIface *iface) {
	IdleServerConnection *conn = IDLE_SERVER_CONNECTION(iface);
	IdleServerConnectionPrivate *priv = IDLE_SERVER_CONNECTION_GET_PRIVATE(conn);

	return priv->state;
}

static void _server_connection_iface_init(gpointer g_iface, gpointer iface_data) {
	IdleServerConnectionIfaceClass *klass = (IdleServerConnectionIfaceClass *)(g_iface);

	klass->connect = iface_connect_impl;
	klass->disconnect = iface_disconnect_impl;
	klass->send = iface_send_impl;
	klass->get_state = iface_get_state_impl;
}
