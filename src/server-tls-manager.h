/*
 * server-tls-manager.h - Header for IdleServerTLSManager
 * Copyright (C) 2010 Collabora Ltd.
 * @author Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef __IDLE_SERVER_TLS_MANAGER_H__
#define __IDLE_SERVER_TLS_MANAGER_H__

#include <glib-object.h>
#include <wocky/wocky.h>
#include <telepathy-glib/telepathy-glib.h>

#include "extensions/extensions.h"

G_BEGIN_DECLS

typedef struct _IdleServerTLSManager IdleServerTLSManager;
typedef struct _IdleServerTLSManagerClass IdleServerTLSManagerClass;
typedef struct _IdleServerTLSManagerPrivate IdleServerTLSManagerPrivate;

struct _IdleServerTLSManagerClass {
  WockyTLSHandlerClass parent_class;
};

struct _IdleServerTLSManager {
  WockyTLSHandler parent;
  IdleServerTLSManagerPrivate *priv;
};

GType idle_server_tls_manager_get_type (void);

#define IDLE_TYPE_SERVER_TLS_MANAGER \
  (idle_server_tls_manager_get_type ())
#define IDLE_SERVER_TLS_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), IDLE_TYPE_SERVER_TLS_MANAGER, \
      IdleServerTLSManager))
#define IDLE_SERVER_TLS_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), IDLE_TYPE_SERVER_TLS_MANAGER, \
      IdleServerTLSManagerClass))
#define IDLE_IS_SERVER_TLS_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_SERVER_TLS_MANAGER))
#define IDLE_IS_SERVER_TLS_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_SERVER_TLS_MANAGER))
#define IDLE_SERVER_TLS_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), IDLE_TYPE_SERVER_TLS_MANAGER, \
      IdleServerTLSManagerClass))

#define IDLE_SERVER_TLS_ERROR idle_server_tls_error_quark ()
GQuark idle_server_tls_error_quark (void);

void idle_server_tls_manager_get_rejection_details (
    IdleServerTLSManager *self,
    gchar **dbus_error,
    GHashTable **details,
    TpConnectionStatusReason *reason);

G_END_DECLS

#endif /* #ifndef __IDLE_SERVER_TLS_MANAGER_H__ */
