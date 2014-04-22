/*
 * server-tls-channel.h - Header for IdleServerTLSChannel
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

#ifndef __IDLE_SERVER_TLS_CHANNEL_H__
#define __IDLE_SERVER_TLS_CHANNEL_H__

#include <glib-object.h>

#include <telepathy-glib/telepathy-glib.h>

#include "tls-certificate.h"

G_BEGIN_DECLS

typedef struct _IdleServerTLSChannelPrivate IdleServerTLSChannelPrivate;
typedef struct _IdleServerTLSChannelClass IdleServerTLSChannelClass;
typedef struct _IdleServerTLSChannel IdleServerTLSChannel;

struct _IdleServerTLSChannelClass {
  TpBaseChannelClass base_class;
};

struct _IdleServerTLSChannel {
  TpBaseChannel parent;

  IdleServerTLSChannelPrivate *priv;
};

GType idle_server_tls_channel_get_type (void);

#define IDLE_TYPE_SERVER_TLS_CHANNEL \
  (idle_server_tls_channel_get_type ())
#define IDLE_SERVER_TLS_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), IDLE_TYPE_SERVER_TLS_CHANNEL, \
      IdleServerTLSChannel))
#define IDLE_SERVER_TLS_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), IDLE_TYPE_SERVER_TLS_CHANNEL, \
      IdleServerTLSChannelClass))
#define IDLE_IS_SERVER_TLS_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_SERVER_TLS_CHANNEL))
#define IDLE_IS_SERVER_TLS_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_SERVER_TLS_CHANNEL))
#define IDLE_SERVER_TLS_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), IDLE_TYPE_SERVER_TLS_CHANNEL,\
      IdleServerTLSChannelClass))

IdleTLSCertificate * idle_server_tls_channel_get_certificate (
    IdleServerTLSChannel *self);

G_END_DECLS

#endif /* #ifndef __IDLE_SERVER_TLS_CHANNEL_H__*/
