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
#include "idle-dns-resolver.h"

#define idle_dns_result_new() \
	(g_slice_new(IdleDNSResult))
#define idle_dns_result_new0() \
	(g_slice_new0(IdleDNSResult))

void idle_dns_result_destroy(IdleDNSResult *result)
{
	if (result->ai_next)
	{
		idle_dns_result_destroy(result->ai_next);
	}
	
	g_slice_free(IdleDNSResult, result);
}

/* don't use crappy sofia */
#if 0
#include "gintset.h"

#define SU_ROOT_MAGIC_T IdleDNSResolver
#include <sofia-sip/su_source.h>
#include <sofia-sip/su_wait.h>
#define HAVE_SU_WAIT_H 1
#include <sofia-sip/sresolv.h>

#include <glib.h>

#include <string.h>

struct _IdleDNSResolver
{
	sres_resolver_t *resolver;
	su_root_t *root;

	GHashTable *queries;

	guint source_id;

	guint serial;
};

typedef struct _IdleDNSQueryData IdleDNSQueryData;

struct _IdleDNSQueryData
{
	IdleDNSResultCallback callback;
	guint port;
	guint query_id;
	guint refcount;
	GIntSet *pending_families;
	IdleDNSResult *results;
	gpointer user_data;
};

typedef struct _IdleDNSQueryInstance IdleDNSQueryInstance;

struct _IdleDNSQueryInstance
{
	IdleDNSQueryData *data;
	uint16_t type;
};

#define idle_dns_query_data_new() \
	(g_slice_new(IdleDNSQueryData))
	
#define idle_dns_query_data_new0() \
	(g_slice_new0(IdleDNSQueryData))

static IdleDNSQueryData *idle_dns_query_data_ref(IdleDNSQueryData *data)
{
	data->refcount++;
	return data;
}

static void idle_dns_query_data_unref(IdleDNSQueryData *data)
{
	if (!--data->refcount)
	{
		g_intset_destroy(data->pending_families);
		g_slice_free(IdleDNSQueryData, data);
	}
}

#define idle_dns_query_instance_new() \
	(g_slice_new(IdleDNSQueryInstance))

#define idle_dns_query_instance_new0() \
	(g_slice_new0(IdleDNSQueryInstance))

static void idle_dns_query_instance_destroy(IdleDNSQueryInstance *instance)
{
	if (instance->data)
	{
		idle_dns_query_data_unref(instance->data);
		instance->data = NULL;
	}
}

IdleDNSResolver *idle_dns_resolver_new()
{
	IdleDNSResolver *ret = g_slice_new(IdleDNSResolver);
	GSource *source;

	ret->root = su_root_source_create(ret);
	g_assert(ret->root);
	su_root_threading(ret->root, FALSE);
	
	source = su_root_gsource(ret->root);
	g_assert(source != NULL);
	ret->source_id = g_source_attach(source, NULL);
	
	ret->resolver = sres_resolver_create(ret->root, NULL, TAG_END());
	ret->queries = g_hash_table_new_full(g_direct_hash, 
										 g_direct_equal,
										 NULL,
										 (GDestroyNotify)(idle_dns_query_instance_destroy));

	return ret;
}

void idle_dns_resolver_destroy(IdleDNSResolver *res)
{
	if (res->resolver)
	{
		sres_resolver_unref(res->resolver);
		res->resolver = NULL;
	}

	if (res->root)
	{
		su_root_destroy(res->root);
		res->root = NULL;
	}
	
	if (res->source_id)
	{
		g_source_remove(res->source_id);
		res->source_id = 0;
	}
	
	if (res->queries)
	{
		g_hash_table_destroy(res->queries);
		res->queries = NULL;
	}

	g_slice_free(IdleDNSResolver, res);
}

struct _call_callback_helper
{
	guint id;
	IdleDNSResult *results;
	IdleDNSResultCallback callback;
	gpointer user_data;
};

static gboolean call_callback_idle(gpointer user_data)
{
	struct _call_callback_helper *helper = user_data;

	helper->callback(helper->id, helper->results, helper->user_data);

	g_free(user_data);
	return FALSE;
}

static void sres_answer_callback(sres_context_t *ctx, sres_query_t *query, sres_record_t **answers)
{
	IdleDNSResolver *resolver = (IdleDNSResolver *)(ctx);
	IdleDNSQueryInstance *instance;
	IdleDNSQueryData *data;
	IdleDNSResult *results;
	IdleDNSResult *tail;
	int i;
	IdleDNSResultCallback callback;
	gpointer user_data;
	struct _call_callback_helper *helper = g_new0(struct _call_callback_helper, 1);
	guint id;
	guint port;

	instance = g_hash_table_lookup(resolver->queries, query);

	if (!instance)
	{
		g_debug("%s: invalid or cancelled context %p, ignoring", G_STRFUNC, ctx);
		return;
	}

	data = instance->data;

	g_intset_remove(data->pending_families, instance->type);

	callback = data->callback;
	user_data = data->user_data;
	port = data->port;
	id = data->query_id;
	results = data->results;

	for (tail = results; tail && tail->ai_next; tail = tail->ai_next);

	sres_sort_answers(resolver->resolver, answers);

	for (i = 0; answers && answers[i] != NULL; i++)
	{
		IdleDNSResult *result;
		int ai_family;
		int ai_socktype = SOCK_STREAM;
		int ai_protocol = 0;
		struct sockaddr *ai_addr;
		socklen_t ai_addrlen;

		switch (answers[i]->sr_record->r_type)
		{
			case sres_type_a:
			{
				struct sockaddr_in *sin;
				
				sin = g_new(struct sockaddr_in, 1);
				
				sin->sin_family = ai_family = AF_INET;
				sin->sin_port = port;
				sin->sin_addr = answers[i]->sr_a->a_addr;
				
				ai_addrlen = sizeof(struct sockaddr_in);

				ai_addr = (struct sockaddr *)(sin);
			};
			break;
			case sres_type_aaaa:
			{
				struct sockaddr_in6 *sin6;

				sin6 = g_new0(struct sockaddr_in6, 1);

				sin6->sin6_family = ai_family = AF_INET6;
				sin6->sin6_port = port;
				memcpy(sin6->sin6_addr.s6_addr, answers[i]->sr_aaaa->aaaa_addr.u6_addr, 16);

				/* FIXME find out about those flow and scope fields, they are currently just set to zero */

				ai_addrlen = sizeof(struct sockaddr_in6);

				ai_addr = (struct sockaddr *)(sin6);
			};
			break;
			case sres_type_a6:
			{
				struct sockaddr_in6 *sin6;

				sin6 = g_new0(struct sockaddr_in6, 1);

				sin6->sin6_family = ai_family = AF_INET6;
				sin6->sin6_port = port;
				memcpy(sin6->sin6_addr.s6_addr, answers[i]->sr_a6->a6_suffix.u6_addr, 16);

				ai_addrlen = sizeof(struct sockaddr_in6);

				ai_addr = (struct sockaddr *)(sin6);
			}
			break;
			default:
			{
				g_debug("%s: unsupported address family %u encountered, ignoring", G_STRFUNC, answers[i]->sr_record->r_type);
				continue;
			}
			break;
		}

		result = idle_dns_result_new0();

		result->ai_family = ai_family;
		result->ai_socktype = ai_socktype;
		result->ai_protocol = ai_protocol;

		result->ai_addr = ai_addr;
		result->ai_addrlen = ai_addrlen;
		
		if (tail)
		{
			tail->ai_next = result;
		}

		if (!results)
		{
			results = result;
		}

		tail = result;
	}
	
	sres_free_answers(resolver->resolver, answers);

	data->results = results;

	/*
	 * FIXME this sucks. We have to return from this function before we can destroy the resolver (which calling of callback can lead to) so we need to trampoline it with g_idle_add 
	 */

	helper->callback = callback;
	helper->id = id;
	helper->results = results;
	helper->user_data = user_data;

	if (!g_intset_size(data->pending_families))
	{
		g_idle_add(call_callback_idle, helper);
	}

	g_hash_table_remove(resolver->queries, query);
}

guint idle_dns_resolver_query(IdleDNSResolver *resolver, const gchar *name, guint port, IdleDNSResultCallback callback, gpointer user_data)
{
	IdleDNSQueryData *data;
	guint query_id = resolver->serial++;
	sres_query_t *sofia_query;
	static const uint16_t types[] = {sres_type_a, sres_type_aaaa, sres_type_a6};
	int i;

	g_debug("%s: resolving %s:%u", G_STRFUNC, name, port);

	data = idle_dns_query_data_new();

	data->callback = callback;
	data->user_data = user_data;
	data->query_id = query_id;
	data->port = htons((unsigned short)(port));
	data->results = NULL;
	data->refcount = 0;
	data->pending_families = g_intset_new();

	for (i = 0; i < (sizeof(types) / sizeof(uint16_t)); i++)
	{
		IdleDNSQueryInstance *instance = idle_dns_query_instance_new();
		instance->data = idle_dns_query_data_ref(data);
		instance->type = types[i];

		sofia_query = sres_query(resolver->resolver,
								 sres_answer_callback, 
								 (sres_context_t *)(resolver),
								 types[i],
								 name);

		g_intset_add(data->pending_families, types[i]);

		g_hash_table_insert(resolver->queries, sofia_query, instance);
	}

	return query_id;
}

static gboolean queries_remove_foreach_func(gpointer key, gpointer value, gpointer user_data)
{
	IdleDNSQueryData *data = (IdleDNSQueryData *)(value);

	return (data->query_id = GPOINTER_TO_UINT(user_data));
}

void idle_dns_resolver_cancel_query(IdleDNSResolver *resolver, guint query_id)
{
	g_hash_table_foreach_remove(resolver->queries, queries_remove_foreach_func, GUINT_TO_POINTER(query_id));
}
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

typedef struct _IdleDNSQueryData IdleDNSQueryData;

struct _IdleDNSQueryData
{
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

static void idle_dns_query_data_destroy(IdleDNSQueryData *data)
{
	g_free(data->name);
	g_slice_free(IdleDNSQueryData, data);
}

struct _IdleDNSResolver
{
	GHashTable *queries;
	guint serial;
};

IdleDNSResolver *
idle_dns_resolver_new()
{
	IdleDNSResolver *ret = g_slice_new(IdleDNSResolver);

	ret->queries = g_hash_table_new_full(g_direct_hash,
										 g_direct_equal,
										 NULL,
										 (GDestroyNotify)(idle_dns_query_data_destroy));
	ret->serial = 0;

	return ret;
}

void
idle_dns_resolver_destroy(IdleDNSResolver *res)
{
	g_hash_table_destroy(res->queries);
	g_slice_free(IdleDNSResolver, res);
}

struct _idle_helper
{
	IdleDNSResolver *res;
	guint serial;
};

#define _idle_helper_new() \
	(g_slice_new(struct _idle_helper))
#define _idle_helper_new0() \
	(g_slice_new0(struct _idle_helper))

static void
_idle_helper_destroy(struct _idle_helper *helper)
{
	g_slice_free(struct _idle_helper, helper);
}

static gboolean
_resolve_idle_func(struct _idle_helper *helper)
{
	IdleDNSQueryData *data = g_hash_table_lookup(helper->res->queries,
			GUINT_TO_POINTER(helper->serial));
	struct addrinfo *info = NULL;
	struct addrinfo *cur;
	int rc;
	IdleDNSResult *results = NULL, *tail = NULL;
	IdleDNSResultCallback cb;
	gpointer user_data;
	gchar *service = g_strdup_printf("%u", data->port);

	cb = data->cb;
	user_data = data->user_data;

	rc = getaddrinfo(data->name, service, NULL, &info);

	if (rc)
	{
		g_debug("%s: getaddrinfo(): %s", G_STRFUNC, gai_strerror(rc));
		return FALSE;
	}

	for (cur = info; cur != NULL; cur = cur->ai_next)
	{
		IdleDNSResult *result = idle_dns_result_new();
		g_debug("%s: got result with family %u", G_STRFUNC, cur->ai_family);

		result->ai_family = cur->ai_family;
		result->ai_socktype = cur->ai_socktype;
		result->ai_protocol = cur->ai_protocol;
		result->ai_addr = cur->ai_addr;
		result->ai_addrlen = cur->ai_addrlen;
		result->ai_next = NULL;

		if (tail)
		{
			tail->ai_next = result;
		}

		if (!results)
		{
			results = result;
		}

		tail = result;
	}

	g_hash_table_remove(helper->res->queries, GUINT_TO_POINTER(helper->serial));
	cb(helper->serial, results, user_data);
	freeaddrinfo(info);
	g_free(service);

	return FALSE;
}

guint
idle_dns_resolver_query(IdleDNSResolver *res, const gchar *name, guint port, IdleDNSResultCallback cb, gpointer user_data)
{
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
	data->source_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
									  (GSourceFunc)(_resolve_idle_func),
									  helper,
									  (GDestroyNotify)(_idle_helper_destroy));

	g_hash_table_insert(res->queries, GUINT_TO_POINTER(ret), data);
	
	return ret;
}

void
idle_dns_resolver_cancel_query(IdleDNSResolver *res, guint id)
{
	IdleDNSQueryData *data;

	g_assert(res);

	data = g_hash_table_lookup(res->queries, GUINT_TO_POINTER(id));

	if (!data)
	{
		g_debug("%s query %u not found!", G_STRFUNC, id);
		return;
	}

	g_source_remove(data->source_id);
	g_hash_table_remove(res->queries, GUINT_TO_POINTER(id));
}

