/*
 * protocol.h - IdleProtocol
 * Copyright © 2007-2010 Collabora Ltd.
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

#ifndef IDLE_PROTOCOL_H
#define IDLE_PROTOCOL_H

#include <glib-object.h>
#include <telepathy-glib/base-protocol.h>

G_BEGIN_DECLS

typedef struct _IdleProtocol IdleProtocol;
typedef struct _IdleProtocolPrivate IdleProtocolPrivate;
typedef struct _IdleProtocolClass IdleProtocolClass;
typedef struct _IdleProtocolClassPrivate IdleProtocolClassPrivate;

struct _IdleProtocolClass {
    TpBaseProtocolClass parent_class;

    IdleProtocolClassPrivate *priv;
};

struct _IdleProtocol {
    TpBaseProtocol parent;

    IdleProtocolPrivate *priv;
};

GType idle_protocol_get_type (void);

#define IDLE_TYPE_PROTOCOL \
    (idle_protocol_get_type ())
#define IDLE_PROTOCOL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
        IDLE_TYPE_PROTOCOL, \
        IdleProtocol))
#define IDLE_PROTOCOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), \
        IDLE_TYPE_PROTOCOL, \
        IdleProtocolClass))
#define IDLE_IS_PROTOCOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), \
        IDLE_TYPE_PROTOCOL))
#define IDLE_PROTOCOL_GET_CLASS(klass) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), \
        IDLE_TYPE_PROTOCOL, \
        IdleProtocolClass))

TpBaseProtocol *idle_protocol_new (void);

G_END_DECLS

#endif
