/*
 * tls-certificate.h - Header for IdleTLSCertificate
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

#ifndef __IDLE_TLS_CERTIFICATE_H__
#define __IDLE_TLS_CERTIFICATE_H__

#include <glib-object.h>

#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

typedef struct _IdleTLSCertificate IdleTLSCertificate;
typedef struct _IdleTLSCertificateClass IdleTLSCertificateClass;
typedef struct _IdleTLSCertificatePrivate IdleTLSCertificatePrivate;

struct _IdleTLSCertificateClass {
  GObjectClass parent_class;

  TpDBusPropertiesMixinClass dbus_props_class;
};

struct _IdleTLSCertificate {
  GObject parent;

  IdleTLSCertificatePrivate *priv;
};

GType idle_tls_certificate_get_type (void);

#define IDLE_TYPE_TLS_CERTIFICATE \
  (idle_tls_certificate_get_type ())
#define IDLE_TLS_CERTIFICATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), IDLE_TYPE_TLS_CERTIFICATE, \
      IdleTLSCertificate))
#define IDLE_TLS_CERTIFICATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), IDLE_TYPE_TLS_CERTIFICATE, \
      IdleTLSCertificateClass))
#define IDLE_IS_TLS_CERTIFICATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_TLS_CERTIFICATE))
#define IDLE_IS_TLS_CERTIFICATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_TLS_CERTIFICATE))
#define IDLE_TLS_CERTIFICATE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), IDLE_TYPE_TLS_CERTIFICATE, \
      IdleTLSCertificateClass))

G_END_DECLS

#endif /* #ifndef __IDLE_TLS_CERTIFICATE_H__*/
