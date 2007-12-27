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
#include "idle-dns-resolver.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_DNS
#include "idle-debug.h"

typedef struct _IdleDNSResultReal IdleDNSResultReal;

struct _IdleDNSResultReal {
  IdleDNSResult result;
  struct addrinfo *addrinfo;
};

#define idle_dns_result_real_new() \
	(g_slice_new(IdleDNSResultReal))
#define idle_dns_result_real_new0() \
	(g_slice_new0(IdleDNSResultReal))

void idle_dns_result_destroy(IdleDNSResult *result) {
  IdleDNSResultReal *real = (IdleDNSResultReal *)(result);

	if (result->ai_next)
		idle_dns_result_destroy(result->ai_next);

  if (real->addrinfo)
    freeaddrinfo(real->addrinfo);

	g_slice_free(IdleDNSResult, result);
}

typedef struct _IdleDNSQueryData IdleDNSQueryData;

struct _IdleDNSQueryData {
	gchar *name;
	guint port;
	IdleDNSResultCallback cb;
	gpointer user_data;
	guint source_id;
};

#define idle_dns_query_data_new() \
	(g_slice_new(IdleDNSQueryData))
#define idle_dns_query_data_new0() \
	(g_slice_new0(IdleDNSQueryData))

static void idle_dns_query_data_destroy(IdleDNSQueryData *data) {
	g_free(data->name);
	g_slice_free(IdleDNSQueryData, data);
}

struct _IdleDNSResolver {
	GHashTable *queries;
	guint serial;
};

IdleDNSResolver *idle_dns_resolver_new() {
	IdleDNSResolver *ret = g_slice_new(IdleDNSResolver);

	ret->queries = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)(idle_dns_query_data_destroy));
	ret->serial = 0;

	return ret;
}

void idle_dns_resolver_destroy(IdleDNSResolver *res) {
	g_hash_table_destroy(res->queries);
	g_slice_free(IdleDNSResolver, res);
}

struct _idle_helper {
	IdleDNSResolver *res;
	guint serial;
};

#define _idle_helper_new() \
	(g_slice_new(struct _idle_helper))
#define _idle_helper_new0() \
	(g_slice_new0(struct _idle_helper))

static void _idle_helper_destroy(struct _idle_helper *helper) {
	g_slice_free(struct _idle_helper, helper);
}

static gboolean _resolve_idle_func(struct _idle_helper *helper) {
	IdleDNSQueryData *data = g_hash_table_lookup(helper->res->queries, GUINT_TO_POINTER(helper->serial));
	struct addrinfo *info = NULL;
	struct addrinfo *cur;
	int rc;
	IdleDNSResultReal *results = NULL, *tail = NULL;
	IdleDNSResultCallback cb;
	gpointer user_data;
	gchar *service = g_strdup_printf("%u", data->port);

	cb = data->cb;
	user_data = data->user_data;

	rc = getaddrinfo(data->name, service, NULL, &info);

	if (rc) {
		IDLE_DEBUG("getaddrinfo(): %s", gai_strerror(rc));
		return FALSE;
	}

	for (cur = info; cur != NULL; cur = cur->ai_next) {
		IdleDNSResultReal *real = idle_dns_result_real_new();
    IdleDNSResult *result = &(real->result);

		result->ai_family = cur->ai_family;
		result->ai_socktype = cur->ai_socktype;
		result->ai_protocol = cur->ai_protocol;
		result->ai_addr = cur->ai_addr;
		result->ai_addrlen = cur->ai_addrlen;
		result->ai_next = NULL;

    real->addrinfo = NULL;

		if (tail)
			tail->result.ai_next = (IdleDNSResult *)(real);

		if (!results) {
			results = real;
      real->addrinfo = info;
		}

		tail = real;
	}

	g_hash_table_remove(helper->res->queries, GUINT_TO_POINTER(helper->serial));
	cb(helper->serial, (IdleDNSResult *)(results), user_data);
	g_free(service);

	return FALSE;
}

guint idle_dns_resolver_query(IdleDNSResolver *res, const gchar *name, guint port, IdleDNSResultCallback cb, gpointer user_data) {
	IdleDNSQueryData *data;
	struct _idle_helper *helper;
	guint ret;

	g_assert (res != NULL);
	g_assert (name != NULL);
	g_assert (port != 0);
	g_assert (cb != NULL);

	ret = res->serial++;

	helper = _idle_helper_new();
	helper->res = res;
	helper->serial = ret;

	data = idle_dns_query_data_new();

	data->name = g_strdup(name);
	data->port = port;
	data->cb = cb;
	data->user_data = user_data;
	data->source_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, (GSourceFunc)(_resolve_idle_func), helper, (GDestroyNotify)(_idle_helper_destroy));

	g_hash_table_insert(res->queries, GUINT_TO_POINTER(ret), data);

	return ret;
}

void idle_dns_resolver_cancel_query(IdleDNSResolver *res, guint id) {
	IdleDNSQueryData *data;

	g_assert(res);

	data = g_hash_table_lookup(res->queries, GUINT_TO_POINTER(id));

	if (!data) {
		IDLE_DEBUG("query %u not found!", id);
		return;
	}

	g_source_remove(data->source_id);
	g_hash_table_remove(res->queries, GUINT_TO_POINTER(id));
}

