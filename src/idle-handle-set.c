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

#include <glib.h>

#include <telepathy-glib/intset.h>

#include "idle-handles.h"
#include "idle-handles-private.h"

#include "idle-handle-set.h"

struct _IdleHandleSet
{
	IdleHandleStorage *storage;
	TpIntSet *intset;
	TpHandleType type;
};

IdleHandleSet *idle_handle_set_new(IdleHandleStorage *storage, TpHandleType type)
{
	IdleHandleSet *set;

	set = g_new(IdleHandleSet, 1);
	set->storage = storage;
	set->intset = tp_intset_new();
	set->type = type;

	return set;
}

static void freer(IdleHandleSet *set, IdleHandle handle, gpointer userdata)
{
	idle_handle_set_remove(set, handle);
}

void idle_handle_set_destroy(IdleHandleSet *set)
{
	idle_handle_set_foreach(set, freer, NULL);
	tp_intset_destroy(set->intset);
	g_free(set);
}

void idle_handle_set_add(IdleHandleSet *set, IdleHandle handle)
{
	g_return_if_fail(set != NULL);
	g_return_if_fail(handle != 0);

	if (!tp_intset_is_member(set->intset, handle))
	{
		if (!idle_handle_ref(set->storage, set->type, handle))
		{
			return;
		}

		tp_intset_add(set->intset, handle);
	}
}

gboolean idle_handle_set_remove(IdleHandleSet *set, IdleHandle handle)
{
	if ((set == NULL) || (handle == 0))
	{
		return FALSE;
	}

	if (tp_intset_is_member(set->intset, handle))
	{
		if (idle_handle_unref(set->storage, set->type, handle))
		{
			tp_intset_remove(set->intset, handle);
			return TRUE;
		}
		else
		{
			return FALSE;
		}
	}
	else
	{
		return FALSE;
	}
}

gboolean idle_handle_set_contains(IdleHandleSet *set, IdleHandle handle)
{
	return tp_intset_is_member(set->intset, handle);
}

typedef struct __idle_handle_set_foreach_data
{
	IdleHandleSet *set;
	IdleHandleFunc func;
	gpointer userdata;
} _idle_handle_set_foreach_data;

static void foreach_helper(guint i, gpointer userdata)
{
	_idle_handle_set_foreach_data *data;

	data = (_idle_handle_set_foreach_data *)(userdata);
	
	data->func(data->set, i, data->userdata);
}

void idle_handle_set_foreach(IdleHandleSet *set, IdleHandleFunc func, gpointer userdata)
{
	_idle_handle_set_foreach_data data = {set, func, userdata};
	tp_intset_foreach(set->intset, foreach_helper, &data);
}

gint idle_handle_set_size(IdleHandleSet *set)
{
	return (set != NULL) ? tp_intset_size(set->intset) : 0;
}

GArray *idle_handle_set_to_array(IdleHandleSet *set)
{
	if (set != NULL)
	{
		return tp_intset_to_array(set->intset);
	}
	else
	{
		return NULL;
	}
}

static void _idle_handle_set_ref_one(guint handle, gpointer data)
{
	IdleHandleSet *set;

	set = (IdleHandleSet *)(data);
	idle_handle_ref(set->storage, set->type, handle);
}

TpIntSet *idle_handle_set_update(IdleHandleSet *set, const TpIntSet *add)
{
	TpIntSet *ret, *tmp;

	if ((set == NULL) || (add == NULL))
	{
		return NULL;
	}

	ret = tp_intset_difference(add, set->intset);
	tp_intset_foreach(ret, _idle_handle_set_ref_one, set);

	tmp = tp_intset_union(add, set->intset);
	tp_intset_destroy(set->intset);
	set->intset = tmp;

	return ret;
}

static void _idle_handle_set_unref_one(guint handle, gpointer data)
{
	IdleHandleSet *set = (IdleHandleSet *)(data);
	idle_handle_unref(set->storage, set->type, handle);
}

TpIntSet *idle_handle_set_difference_update(IdleHandleSet *set, const TpIntSet *remove)
{
	TpIntSet *ret, *tmp;

	if ((set == NULL) || (remove == NULL))
	{
		return NULL;
	}

	ret = tp_intset_intersection(remove, set->intset);
	tp_intset_foreach(ret, _idle_handle_set_unref_one, set);

	tmp = tp_intset_difference(set->intset, remove);
	tp_intset_destroy(set->intset);
	set->intset = tmp;

	return ret;
}
