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
#include <glib-object.h>

#include <telepathy-glib/errors.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "idle-ssl-server-connection.h"
#include "idle-server-connection-iface.h"
#include "idle-connection.h"

#include "idle-dns-resolver.h"

#define IDLE_DEBUG_FLAG IDLE_DEBUG_NETWORK
#include "idle-debug.h"

static void _server_connection_iface_init(gpointer, gpointer);
typedef struct _IdleSSLServerConnectionPrivate IdleSSLServerConnectionPrivate;

#define IDLE_SSL_SERVER_CONNECTION_GET_PRIVATE(conn) \
	(G_TYPE_INSTANCE_GET_PRIVATE((conn), IDLE_TYPE_SSL_SERVER_CONNECTION, \
								 IdleSSLServerConnectionPrivate))

G_DEFINE_TYPE_WITH_CODE(IdleSSLServerConnection, idle_ssl_server_connection, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(IDLE_TYPE_SERVER_CONNECTION_IFACE, _server_connection_iface_init));

enum {
	PROP_HOST = 1,
	PROP_PORT,
};

typedef void (*async_connecting_finished_cb)(IdleSSLServerConnection *conn, gboolean success);

typedef struct _AsyncConnectData AsyncConnectData;
struct _AsyncConnectData {
	IdleSSLServerConnection *conn;

	guint watch_id;
	GIOChannel *io_chan;
	guint fd;

	IdleDNSResult *res;
	IdleDNSResult *cur;

	async_connecting_finished_cb finished_cb;
};

#define async_connect_data_new() \
	(g_slice_new(AsyncConnectData))
#define async_connect_data_new0() \
	(g_slice_new0(AsyncConnectData))

static void async_connect_data_destroy(AsyncConnectData *data) {
	if (data->watch_id) {
		g_source_remove(data->watch_id);
		data->watch_id = 0;
	}

	if (data->io_chan) {
		g_io_channel_shutdown(data->io_chan, FALSE, NULL);
		g_io_channel_unref(data->io_chan);
		data->io_chan = NULL;
	}

	if (data->fd) {
		close(data->fd);
		data->fd = 0;
	}

	if (data->res) {
		idle_dns_result_destroy(data->res);
		data->res = NULL;
		data->cur = NULL;
	}

	g_slice_free(AsyncConnectData, data);
}

struct _IdleSSLServerConnectionPrivate {
	gchar *host;
	guint port;

	GIOChannel *io_chan;
	int fd;
	SSL *ssl;
	BIO *bio;

	guint read_watch_id;
	guint last_message_sent;

	IdleDNSResolver *resolver;
	AsyncConnectData *connect_data;

	IdleServerConnectionState state;

	gboolean dispose_has_run;
};

static GObject *idle_ssl_server_connection_constructor(GType type, guint n_props, GObjectConstructParam *props);

static void idle_ssl_server_connection_init(IdleSSLServerConnection *conn) {
	IdleSSLServerConnectionPrivate *priv = IDLE_SSL_SERVER_CONNECTION_GET_PRIVATE(conn);

	priv->host = NULL;
	priv->port = 0;

	priv->io_chan = NULL;
	priv->fd = 0;
	priv->ssl = NULL;
	priv->bio = NULL;

	priv->read_watch_id = 0;
	priv->last_message_sent = 0;

	priv->resolver = idle_dns_resolver_new();
	priv->connect_data = NULL;

	priv->state = SERVER_CONNECTION_STATE_NOT_CONNECTED;

	priv->dispose_has_run = FALSE;
}

static GObject *idle_ssl_server_connection_constructor(GType type, guint n_props, GObjectConstructParam *props) {
	GObject *ret;

	ret = G_OBJECT_CLASS(idle_ssl_server_connection_parent_class)->constructor(type, n_props, props);

	return ret;
}

static void idle_ssl_server_connection_dispose(GObject *obj) {
	IdleSSLServerConnection *conn = IDLE_SSL_SERVER_CONNECTION(obj);
	IdleSSLServerConnectionPrivate *priv = IDLE_SSL_SERVER_CONNECTION_GET_PRIVATE(conn);

	if (priv->dispose_has_run)
		return;

	IDLE_DEBUG("dispose called");
	priv->dispose_has_run = TRUE;

	if (priv->state == SERVER_CONNECTION_STATE_CONNECTED) {
		GError *error = NULL;
		g_warning("%s: connection was open when the object was deleted, it'll probably crash now..", G_STRFUNC);

		if (!idle_server_connection_iface_disconnect(IDLE_SERVER_CONNECTION_IFACE(obj), &error)) {
			g_error_free(error);
		}
	}

	if (priv->ssl)
		SSL_free(priv->ssl);

	if (priv->bio)
		BIO_free(priv->bio);

	if (priv->read_watch_id)
		g_source_remove(priv->read_watch_id);
}

static void idle_ssl_server_connection_finalize(GObject *obj) {
	IdleSSLServerConnection *conn = IDLE_SSL_SERVER_CONNECTION(obj);
	IdleSSLServerConnectionPrivate *priv = IDLE_SSL_SERVER_CONNECTION_GET_PRIVATE(conn);

	g_free(priv->host);

	idle_dns_resolver_destroy(priv->resolver);

	if (priv->connect_data)
		async_connect_data_destroy(priv->connect_data);
}

static void idle_ssl_server_connection_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec) {
	IdleSSLServerConnection *conn = IDLE_SSL_SERVER_CONNECTION(obj);
	IdleSSLServerConnectionPrivate *priv = IDLE_SSL_SERVER_CONNECTION_GET_PRIVATE(conn);

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

static void idle_ssl_server_connection_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec) {
	IdleSSLServerConnection *conn = IDLE_SSL_SERVER_CONNECTION(obj);
	IdleSSLServerConnectionPrivate *priv = IDLE_SSL_SERVER_CONNECTION_GET_PRIVATE(conn);

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

static void idle_ssl_server_connection_class_init(IdleSSLServerConnectionClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	g_type_class_add_private(klass, sizeof(IdleSSLServerConnectionPrivate));

	object_class->constructor = idle_ssl_server_connection_constructor;
	object_class->dispose = idle_ssl_server_connection_dispose;
	object_class->finalize = idle_ssl_server_connection_finalize;

	object_class->get_property = idle_ssl_server_connection_get_property;
	object_class->set_property = idle_ssl_server_connection_set_property;

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

static void ssl_conn_change_state(IdleSSLServerConnection *conn, guint state, guint reason) {
	IdleSSLServerConnectionPrivate *priv = IDLE_SSL_SERVER_CONNECTION_GET_PRIVATE(conn);

	if (state == priv->state)
		return;

	priv->state = state;

	g_signal_emit_by_name(conn, "status-changed", state, reason);
}

static SSL_CTX *ssl_conn_get_ctx() {
	static SSL_CTX *ctx = NULL;

	if (!ctx) {
		SSL_library_init();
		SSL_load_error_strings();

		ctx = SSL_CTX_new(SSLv23_client_method());

		if (!ctx) {
			IDLE_DEBUG("OpenSSL initialization failed!");
		}
	}

	return ctx;
}

static gboolean iface_ssl_disconnect_impl_full(IdleServerConnectionIface *iface, guint reason, GError **error);

static gboolean ssl_io_err_cleanup_func(gpointer user_data) {
	IdleServerConnectionIface *iface = IDLE_SERVER_CONNECTION_IFACE(user_data);
	GError *error;

	if (!iface_ssl_disconnect_impl_full(iface, SERVER_CONNECTION_STATE_REASON_ERROR, &error)) {
		IDLE_DEBUG("disconnect: %s", error->message);
		g_error_free(error);
	}

	return FALSE;
}

static gboolean ssl_io_func(GIOChannel *src, GIOCondition cond, gpointer data) {
	IdleSSLServerConnection *conn = IDLE_SSL_SERVER_CONNECTION(data);
	IdleSSLServerConnectionPrivate *priv = IDLE_SSL_SERVER_CONNECTION_GET_PRIVATE(data);
	gchar buf[IRC_MSG_MAXLEN + 3];
	int err;

	if ((cond == G_IO_ERR) || (cond == G_IO_HUP)) {
		IDLE_DEBUG("got G_IO_ERR || G_IO_HUP");
		ssl_conn_change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, SERVER_CONNECTION_STATE_REASON_ERROR);

		priv->read_watch_id = 0;
		return FALSE;
	}

	memset(buf, 0, IRC_MSG_MAXLEN + 3);

	do {
		err = SSL_read(priv->ssl, buf, IRC_MSG_MAXLEN + 2);
	} while ((err <= 0) && (SSL_get_error(priv->ssl, err) == SSL_ERROR_WANT_READ));

	if (err <= 0) {
		IDLE_DEBUG("SSL_read failed with error %i", SSL_get_error(priv->ssl, err));

		g_idle_add(ssl_io_err_cleanup_func, conn);

		priv->read_watch_id = 0;
		return FALSE;
	}

	g_signal_emit_by_name(conn, "received", buf);

	return TRUE;
}

static void ssl_async_connecting_finished_cb(IdleSSLServerConnection *conn, gboolean success) {
	IdleSSLServerConnectionPrivate *priv = IDLE_SSL_SERVER_CONNECTION_GET_PRIVATE(conn);
	SSL_CTX *ctx;
	X509 *cert;
	int status;
	int opt;

	if (!success)
		return ssl_conn_change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, SERVER_CONNECTION_STATE_REASON_ERROR);

	priv->fd = priv->connect_data->fd;
	priv->connect_data->fd = 0;

	priv->io_chan = priv->connect_data->io_chan;
	priv->connect_data->io_chan = NULL;

	if (priv->connect_data->watch_id) {
		g_source_remove(priv->connect_data->watch_id);
		priv->connect_data->watch_id = 0;
	}

	priv->read_watch_id = g_io_add_watch(priv->io_chan, G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP, ssl_io_func, conn);

	if (fcntl(priv->fd, F_SETFL, 0))
		IDLE_DEBUG("failed to set socket back to blocking mode");

	opt = 1;
	setsockopt(priv->fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

	opt = 1;
	setsockopt(priv->fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

	ctx = ssl_conn_get_ctx();

	if (!ctx) {
		IDLE_DEBUG("failed to get SSL context object");
		return ssl_async_connecting_finished_cb(conn, FALSE);
	}

	priv->ssl = SSL_new(ctx);

	if (!priv->ssl) {
		IDLE_DEBUG("failed to create SSL object");
		return ssl_async_connecting_finished_cb(conn, FALSE);
	}

	status = SSL_set_fd(priv->ssl, priv->fd);

	if (!status) {
		IDLE_DEBUG("failed to set SSL socket");
		return ssl_async_connecting_finished_cb(conn, FALSE);
	}

	status = SSL_connect(priv->ssl);

	if (status <= 0) {
		IDLE_DEBUG("SSL_connect failed with status %i (error %i)", status, SSL_get_error(priv->ssl, status));
		return ssl_async_connecting_finished_cb(conn, FALSE);
	}

	cert = SSL_get_peer_certificate(priv->ssl);

	if (!cert) {
		IDLE_DEBUG("failed to get SSL peer certificate");
		return ssl_async_connecting_finished_cb(conn, FALSE);
	}

	/* TODO sometime in the future implement certificate verification */

	X509_free(cert);

	ssl_conn_change_state(conn, SERVER_CONNECTION_STATE_CONNECTED, SERVER_CONNECTION_STATE_REASON_REQUESTED);
}

static void ssl_do_connect(AsyncConnectData *data);

static gboolean ssl_connect_io_func(GIOChannel *src, GIOCondition cond, gpointer user_data) {
	AsyncConnectData *data = (AsyncConnectData *)(user_data);
	int optval;
	socklen_t optlen = sizeof(optval);

	g_assert(getsockopt(data->fd, SOL_SOCKET, SO_ERROR, &optval, &optlen) == 0);

	if (optval == 0) {
		data->finished_cb(data->conn, TRUE);
	} else {
		g_source_remove(data->watch_id);
		data->watch_id = 0;

		g_io_channel_shutdown(data->io_chan, FALSE, NULL);
		data->io_chan = NULL;

		close(data->fd);
		data->fd = 0;

		ssl_do_connect(data);
	}

	return FALSE;
}

static void ssl_do_connect(AsyncConnectData *data) {
	int fd = -1, rc = -1;
	GIOChannel *io_chan;

	for (; data->cur != NULL; data->cur = data->cur->ai_next) {
		fd = socket(data->cur->ai_family, data->cur->ai_socktype, data->cur->ai_protocol);

		if (fd == -1)
			IDLE_DEBUG("socket() failed: %s", g_strerror(errno));
		else
			break;
	}

	if (fd == -1) {
		IDLE_DEBUG("failed: %s", g_strerror(errno));
		return data->finished_cb(data->conn, FALSE);
	}

	rc = fcntl(fd, F_SETFL, O_NONBLOCK);

	if (rc != 0) {
		IDLE_DEBUG("failed to set socket to non-blocking mode: %s", g_strerror(errno));
		close(fd);
		return data->finished_cb(data->conn, FALSE);
	}

	rc = connect(fd, data->cur->ai_addr, data->cur->ai_addrlen);

	g_assert(rc == -1);

	if (errno != EINPROGRESS) {
		IDLE_DEBUG("connect() failed: %s", g_strerror(errno));
		close(fd);
		return data->finished_cb(data->conn, FALSE);
	}

	io_chan = g_io_channel_unix_new(fd);
	g_io_channel_set_encoding(io_chan, NULL, NULL);
	g_io_channel_set_buffered(io_chan, FALSE);

	data->fd = fd;
	data->io_chan = io_chan;
	data->watch_id = g_io_add_watch(io_chan, G_IO_OUT | G_IO_ERR, ssl_connect_io_func, data);
}

static void ssl_dns_result_cb(guint unused, IdleDNSResult *results, gpointer user_data) {
	IdleSSLServerConnection *conn = IDLE_SSL_SERVER_CONNECTION(user_data);
	IdleSSLServerConnectionPrivate *priv = IDLE_SSL_SERVER_CONNECTION_GET_PRIVATE(conn);
	AsyncConnectData *data;

	if (priv->connect_data) {
		async_connect_data_destroy(priv->connect_data);
	}

	priv->connect_data = data = async_connect_data_new0();

	data->conn = conn;
	data->res = results;
	data->cur = results;
	data->finished_cb = ssl_async_connecting_finished_cb;

	return ssl_do_connect(data);
}

static gboolean iface_ssl_connect_impl(IdleServerConnectionIface *iface, GError **error) {
	IdleSSLServerConnection *conn = IDLE_SSL_SERVER_CONNECTION(iface);
	IdleSSLServerConnectionPrivate *priv = IDLE_SSL_SERVER_CONNECTION_GET_PRIVATE(conn);

	if (priv->state != SERVER_CONNECTION_STATE_NOT_CONNECTED) {
		IDLE_DEBUG("connection was not in state NOT_CONNECTED");

		*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "connection was in state NOT_CONNECTED");

		return FALSE;
	}

	if (!priv->host || !priv->host[0]) {
		IDLE_DEBUG("no hostname provided");

		*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "no hostname provided");

		return FALSE;
	}

	if (!priv->port) {
		IDLE_DEBUG("no port provided");

		*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "no port provided");

		return FALSE;
	}

	idle_dns_resolver_query(priv->resolver, priv->host, priv->port, ssl_dns_result_cb, conn);

	ssl_conn_change_state(conn, SERVER_CONNECTION_STATE_CONNECTING, SERVER_CONNECTION_STATE_REASON_REQUESTED);

	return TRUE;
}

static gboolean iface_ssl_disconnect_impl_full(IdleServerConnectionIface *iface, guint reason, GError **error) {
	IdleSSLServerConnection *conn = IDLE_SSL_SERVER_CONNECTION(iface);
	IdleSSLServerConnectionPrivate *priv = IDLE_SSL_SERVER_CONNECTION_GET_PRIVATE(conn);

	if (priv->state == SERVER_CONNECTION_STATE_NOT_CONNECTED) {
		IDLE_DEBUG("not connected");

		*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "not connected");

		return FALSE;
	}

	if (priv->read_watch_id) {
		g_source_remove(priv->read_watch_id);
		priv->read_watch_id = 0;
	}

	if (priv->io_chan) {
		GError *io_error = NULL;

		g_io_channel_shutdown(priv->io_chan, FALSE, &io_error);

		if (io_error) {
			IDLE_DEBUG("g_io_channel_shutdown failed: %s", io_error->message);

			g_error_free(io_error);
		}

		g_io_channel_unref(priv->io_chan);

		priv->io_chan = NULL;
	}

	if (priv->fd) {
		close(priv->fd);
		priv->fd = 0;
	}

	ssl_conn_change_state(conn, SERVER_CONNECTION_STATE_NOT_CONNECTED, reason);

	return TRUE;
}

static gboolean iface_ssl_disconnect_impl(IdleServerConnectionIface *iface, GError **error) {
	return iface_ssl_disconnect_impl_full(iface, SERVER_CONNECTION_STATE_REASON_REQUESTED, error);
}

static gboolean iface_ssl_send_impl(IdleServerConnectionIface *iface, const gchar *cmd, GError **error) {
	IdleSSLServerConnection *conn = IDLE_SSL_SERVER_CONNECTION(iface);
	IdleSSLServerConnectionPrivate *priv = IDLE_SSL_SERVER_CONNECTION_GET_PRIVATE(conn);
	gsize len;
	int rc;

	g_assert(cmd != NULL);

	if (priv->state != SERVER_CONNECTION_STATE_CONNECTED) {
		IDLE_DEBUG("connection was not in state CONNECTED");

		*error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "connection was not in state CONNECTED");

		return FALSE;
	}

	len = strlen(cmd);

	if (!len) {
		return TRUE;
	}

	rc = SSL_write(priv->ssl, cmd, len);

	if (rc <= 0) {
		GError *local_error;

		IDLE_DEBUG("SSL_write failed with status %i (error %i)", rc, SSL_get_error(priv->ssl, rc));

		if (!iface_ssl_disconnect_impl_full(IDLE_SERVER_CONNECTION_IFACE(conn), SERVER_CONNECTION_STATE_REASON_ERROR, &local_error)) {
			g_error_free(local_error);
		}

		*error = g_error_new(TP_ERRORS, TP_ERROR_NETWORK_ERROR, "SSL_write failed");

		return FALSE;
	}

	return TRUE;
}

IdleServerConnectionState iface_ssl_get_state_impl(IdleServerConnectionIface *iface) {
	IdleSSLServerConnection *conn = IDLE_SSL_SERVER_CONNECTION(iface);
	IdleSSLServerConnectionPrivate *priv = IDLE_SSL_SERVER_CONNECTION_GET_PRIVATE(conn);

	return priv->state;
}

static void _server_connection_iface_init(gpointer g_iface, gpointer iface_data) {
	IdleServerConnectionIfaceClass *klass = (IdleServerConnectionIfaceClass *)(g_iface);

	klass->connect = iface_ssl_connect_impl;
	klass->disconnect = iface_ssl_disconnect_impl;
	klass->send = iface_ssl_send_impl;
	klass->get_state = iface_ssl_get_state_impl;
}
