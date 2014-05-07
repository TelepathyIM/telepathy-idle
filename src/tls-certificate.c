/*
 * tls-certificate.c - Source for IdleTLSCertificate
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

#include "config.h"
#include "tls-certificate.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_TLS
#include "idle-debug.h"

static void
tls_certificate_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (IdleTLSCertificate,
    idle_tls_certificate,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_AUTHENTICATION_TLS_CERTIFICATE,
        tls_certificate_iface_init))

struct _IdleTLSCertificatePrivate {
  gchar *object_path;

  gchar *cert_type;
  TpTLSCertificateState cert_state;

  GPtrArray *rejections;
  GPtrArray *cert_data;

  GDBusConnection *dbus_connection;

  gboolean dispose_has_run;
};

enum {
  PROP_OBJECT_PATH = 1,
  PROP_STATE,
  PROP_REJECTIONS,
  PROP_CERTIFICATE_TYPE,
  PROP_CERTIFICATE_CHAIN_DATA,

  /* not exported */
  PROP_DBUS_CONNECTION,

  NUM_PROPERTIES
};

static void
idle_tls_certificate_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  IdleTLSCertificate *self = IDLE_TLS_CERTIFICATE (object);

  switch (property_id)
    {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, self->priv->object_path);
      break;
    case PROP_STATE:
      g_value_set_uint (value, self->priv->cert_state);
      break;
    case PROP_REJECTIONS:
      g_value_set_boxed (value, self->priv->rejections);
      break;
    case PROP_CERTIFICATE_TYPE:
      g_value_set_string (value, self->priv->cert_type);
      break;
    case PROP_CERTIFICATE_CHAIN_DATA:
      g_value_set_boxed (value, self->priv->cert_data);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
idle_tls_certificate_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  IdleTLSCertificate *self = IDLE_TLS_CERTIFICATE (object);

  switch (property_id)
    {
    case PROP_OBJECT_PATH:
      self->priv->object_path = g_value_dup_string (value);
      break;
    case PROP_CERTIFICATE_TYPE:
      self->priv->cert_type = g_value_dup_string (value);
      break;
    case PROP_CERTIFICATE_CHAIN_DATA:
      self->priv->cert_data = g_value_dup_boxed (value);
      break;
    case PROP_DBUS_CONNECTION:
      self->priv->dbus_connection = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, value);
      break;
    }
}

static void
idle_tls_certificate_finalize (GObject *object)
{
  IdleTLSCertificate *self = IDLE_TLS_CERTIFICATE (object);

  tp_clear_boxed (TP_ARRAY_TYPE_TLS_CERTIFICATE_REJECTION_LIST,
      &self->priv->rejections);

  g_free (self->priv->object_path);
  g_free (self->priv->cert_type);
  g_ptr_array_unref (self->priv->cert_data);

  G_OBJECT_CLASS (idle_tls_certificate_parent_class)->finalize (object);
}

static void
idle_tls_certificate_dispose (GObject *object)
{
  IdleTLSCertificate *self = IDLE_TLS_CERTIFICATE (object);

  if (self->priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  tp_clear_object (&self->priv->dbus_connection);

  G_OBJECT_CLASS (idle_tls_certificate_parent_class)->dispose (object);
}

static void
idle_tls_certificate_constructed (GObject *object)
{
  IdleTLSCertificate *self = IDLE_TLS_CERTIFICATE (object);
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (idle_tls_certificate_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (object);

  /* register the certificate on the bus */
  tp_dbus_connection_register_object (self->priv->dbus_connection,
      self->priv->object_path, self);
}

static void
idle_tls_certificate_init (IdleTLSCertificate *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      IDLE_TYPE_TLS_CERTIFICATE, IdleTLSCertificatePrivate);
  self->priv->rejections = g_ptr_array_new ();
}

static void
idle_tls_certificate_class_init (IdleTLSCertificateClass *klass)
{
  static TpDBusPropertiesMixinPropImpl object_props[] = {
    { "State", "state", NULL },
    { "Rejections", "rejections", NULL },
    { "CertificateType", "certificate-type", NULL },
    { "CertificateChainData", "certificate-chain-data", NULL },
    { NULL }
  };
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (IdleTLSCertificatePrivate));

  oclass->finalize = idle_tls_certificate_finalize;
  oclass->dispose = idle_tls_certificate_dispose;
  oclass->set_property = idle_tls_certificate_set_property;
  oclass->get_property = idle_tls_certificate_get_property;
  oclass->constructed = idle_tls_certificate_constructed;

  pspec = g_param_spec_string ("object-path",
      "D-Bus object path",
      "The D-Bus object path used for this object on the bus.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_OBJECT_PATH, pspec);

  pspec = g_param_spec_uint ("state",
      "State of this certificate",
      "The state of this TLS certificate.",
      0, TP_NUM_TLS_CERTIFICATE_STATES - 1,
      TP_TLS_CERTIFICATE_STATE_PENDING,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_STATE, pspec);

  pspec = g_param_spec_boxed ("rejections",
      "The reject reasons",
      "The reasons why this TLS certificate has been rejected",
      TP_ARRAY_TYPE_TLS_CERTIFICATE_REJECTION_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_REJECTIONS, pspec);

  pspec = g_param_spec_string ("certificate-type",
      "The certificate type",
      "The type of this certificate.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_CERTIFICATE_TYPE, pspec);

  pspec = g_param_spec_boxed ("certificate-chain-data",
      "The certificate chain data",
      "The raw PEM-encoded trust chain of this certificate.",
      TP_ARRAY_TYPE_UCHAR_ARRAY_LIST,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_CERTIFICATE_CHAIN_DATA, pspec);

  pspec = g_param_spec_object ("dbus-connection", "D-Bus connection",
      "D-Bus connection",
      G_TYPE_DBUS_CONNECTION,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_DBUS_CONNECTION, pspec);

  tp_dbus_properties_mixin_class_init (oclass, 0);
  tp_dbus_properties_mixin_implement_interface (oclass,
      TP_IFACE_QUARK_AUTHENTICATION_TLS_CERTIFICATE,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL, object_props);
}

static void
idle_tls_certificate_accept (TpSvcAuthenticationTLSCertificate *cert,
    GDBusMethodInvocation *context)
{
  IdleTLSCertificate *self = IDLE_TLS_CERTIFICATE (cert);

  IDLE_DEBUG ("Accept() called on the TLS certificate; current state %u",
      self->priv->cert_state);

  if (self->priv->cert_state != TP_TLS_CERTIFICATE_STATE_PENDING)
    {
      GError error =
        { TP_ERROR,
          TP_ERROR_INVALID_ARGUMENT,
          "Calling Accept() on a certificate with state != PENDING "
          "doesn't make sense."
        };

      g_dbus_method_invocation_return_gerror (context, &error);
      return;
    }

  self->priv->cert_state = TP_TLS_CERTIFICATE_STATE_ACCEPTED;
  tp_svc_authentication_tls_certificate_emit_accepted (self);

  tp_svc_authentication_tls_certificate_return_from_accept (context);
}

static void
idle_tls_certificate_reject (TpSvcAuthenticationTLSCertificate *cert,
    const GPtrArray *rejections,
    GDBusMethodInvocation *context)
{
  IdleTLSCertificate *self = IDLE_TLS_CERTIFICATE (cert);

  IDLE_DEBUG ("Reject() called on the TLS certificate with rejections %p, "
      "length %u; current state %u", rejections, rejections->len,
      self->priv->cert_state);

  if (rejections->len < 1)
    {
      GError error = { TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Calling Reject() with a zero-length rejection list." };

      g_dbus_method_invocation_return_gerror (context, &error);
      return;
    }

  if (self->priv->cert_state != TP_TLS_CERTIFICATE_STATE_PENDING)
    {
      GError error =
        { TP_ERROR,
          TP_ERROR_INVALID_ARGUMENT,
          "Calling Reject() on a certificate with state != PENDING "
          "doesn't make sense."
        };

      g_dbus_method_invocation_return_gerror (context, &error);
      return;
    }

  tp_clear_boxed (TP_ARRAY_TYPE_TLS_CERTIFICATE_REJECTION_LIST,
      &self->priv->rejections);

  self->priv->rejections =
    g_boxed_copy (TP_ARRAY_TYPE_TLS_CERTIFICATE_REJECTION_LIST,
        rejections);
  self->priv->cert_state = TP_TLS_CERTIFICATE_STATE_REJECTED;

  tp_svc_authentication_tls_certificate_emit_rejected (
      self, self->priv->rejections);

  tp_svc_authentication_tls_certificate_return_from_reject (context);
}

static void
tls_certificate_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpSvcAuthenticationTLSCertificateClass *klass = g_iface;

#define IMPLEMENT(x) \
  tp_svc_authentication_tls_certificate_implement_##x ( \
      klass, idle_tls_certificate_##x)
  IMPLEMENT (accept);
  IMPLEMENT (reject);
#undef IMPLEMENT
}
