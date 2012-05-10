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

#ifndef __IDLE_IM_MANAGER_H__
#define __IDLE_IM_MANAGER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _IdleIMManager IdleIMManager;
typedef struct _IdleIMManagerClass IdleIMManagerClass;

struct _IdleIMManagerClass {
	GObjectClass parent_class;
};

struct _IdleIMManager {
	GObject parent;
};

GType idle_im_manager_get_type (void);

#define IDLE_TYPE_IM_MANAGER (idle_im_manager_get_type())
#define IDLE_IM_MANAGER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), IDLE_TYPE_IM_MANAGER, IdleIMManager))
#define IDLE_IM_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), IDLE_TYPE_IM_MANAGER, IdleIMManagerClass))
#define IDLE_IS_IM_MANAGER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_IM_MANAGER))
#define IDLE_IS_IM_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_IM_MANAGER))
#define IDLE_IM_MANAGER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), IDLE_TYPE_IM_MANAGER, IdleIMManagerClass))

G_END_DECLS

#endif /* #ifndef __IDLE_IM_MANAGER_H__ */

