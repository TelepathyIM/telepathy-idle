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
#define __USE_POSIX
#include <sys/types.h>
#include <netdb.h>
#undef __USE_POSIX

G_BEGIN_DECLS

typedef struct _IdleDNSResolver IdleDNSResolver;
typedef struct _IdleDNSResult IdleDNSResult;

struct _IdleDNSResult {
	/* as passed to socket() */
	int ai_family;
	int ai_socktype;
	int ai_protocol;
	
	/* as passed to connect() */
	struct sockaddr *ai_addr;
	socklen_t ai_addrlen;

	/* pointer to the next list member or NULL if this is the last one */
	IdleDNSResult *ai_next;
};

void idle_dns_result_destroy(IdleDNSResult *result);

/*
 * id: the query identifier as returned by query()
 * results: a linked list of _IdleDNSResult structs which should be freed with idle_dns_result_destroy
 * user_data: the user_data pointer as passed to query()
 */

typedef void (*IdleDNSResultCallback)(guint id, IdleDNSResult *results, gpointer user_data);

IdleDNSResolver *idle_dns_resolver_new();
void idle_dns_resolver_destroy(IdleDNSResolver *);

/*
 * returns: the ID of the query, which can be passed to cancel_query()
 */

guint idle_dns_resolver_query(IdleDNSResolver *, const gchar *name, guint port, IdleDNSResultCallback callback, gpointer user_data);
void idle_dns_resolver_cancel_query(IdleDNSResolver *, guint id);

G_END_DECLS
