/*
 * server-tls-channel.c - Source for IdleServerTLSChannel
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

#include <config.h>

#include "server-tls-channel.h"

#include <gio/gio.h>
#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_TLS
#include "idle-debug.h"
#include "tls-certificate.h"

G_DEFINE_TYPE_WITH_CODE (IdleServerTLSChannel, idle_server_tls_channel,
    TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_SERVER_TLS_CONNECTION,
        NULL));

static void idle_server_tls_channel_close (TpBaseChannel *base);

enum {
  /* server TLS channel iface */
  PROP_SERVER_CERTIFICATE = 1,
  PROP_HOSTNAME,
  PROP_REFERENCE_IDENTITIES,

  /* not exported */
  PROP_CERTIFICATE,

  NUM_PROPERTIES
};

struct _IdleServerTLSChannelPrivate {
  GTlsCertificate *certificate;

  IdleTLSCertificate *server_cert;
  gchar *server_cert_path;
  gchar *hostname;
  GStrv reference_identities;

  gboolean dispose_has_run;
};

static void
idle_server_tls_channel_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  IdleServerTLSChannel *self = IDLE_SERVER_TLS_CHANNEL (object);

  switch (property_id)
    {
    case PROP_SERVER_CERTIFICATE:
      g_value_set_boxed (value, self->priv->server_cert_path);
      break;
    case PROP_HOSTNAME:
      g_value_set_string (value, self->priv->hostname);
      break;
    case PROP_REFERENCE_IDENTITIES:
      g_value_set_boxed (value, self->priv->reference_identities);
      break;
    case PROP_CERTIFICATE:
      g_value_set_object (value, self->priv->certificate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
idle_server_tls_channel_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  IdleServerTLSChannel *self = IDLE_SERVER_TLS_CHANNEL (object);

  switch (property_id)
    {
    case PROP_CERTIFICATE:
      self->priv->certificate = g_value_dup_object (value);
      break;
    case PROP_HOSTNAME:
      self->priv->hostname = g_value_dup_string (value);
      break;
    case PROP_REFERENCE_IDENTITIES:
      self->priv->reference_identities = g_value_dup_boxed (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
idle_server_tls_channel_finalize (GObject *object)
{
  IdleServerTLSChannel *self = IDLE_SERVER_TLS_CHANNEL (object);

  IDLE_DEBUG ("Finalize TLS channel");

  g_free (self->priv->server_cert_path);
  g_free (self->priv->hostname);
  g_strfreev (self->priv->reference_identities);

  G_OBJECT_CLASS (idle_server_tls_channel_parent_class)->finalize (object);
}

static void
idle_server_tls_channel_dispose (GObject *object)
{
  IdleServerTLSChannel *self = IDLE_SERVER_TLS_CHANNEL (object);

  if (self->priv->dispose_has_run)
    return;

  IDLE_DEBUG ("Dispose TLS channel");

  self->priv->dispose_has_run = TRUE;

  tp_clear_object (&self->priv->server_cert);
  tp_clear_object (&self->priv->certificate);

  G_OBJECT_CLASS (idle_server_tls_channel_parent_class)->dispose (object);
}

static void
idle_server_tls_channel_constructed (GObject *object)
{
  IdleServerTLSChannel *self = IDLE_SERVER_TLS_CHANNEL (object);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (idle_server_tls_channel_parent_class)->constructed;
  const gchar *path;
  gchar *cert_object_path;
  GPtrArray *certificates;
  GTlsCertificate *cert;

  if (chain_up != NULL)
    chain_up (object);

  tp_base_channel_register (base);

  /* create the TLS certificate object */
  path = tp_base_channel_get_object_path (base);
  cert_object_path = g_strdup_printf ("%s/TLSCertificateObject", path);
  certificates = g_ptr_array_new ();

  /* Setup the full chain */
  cert = self->priv->certificate;
  while (cert != NULL)
    {
      GByteArray *content;
      GArray *c;

      g_object_get (cert,
          "certificate", &content,
          NULL);
      c = g_array_sized_new (TRUE, TRUE, sizeof (guchar), content->len);
      g_array_append_vals (c, content->data, content->len);
      g_ptr_array_add (certificates, c);

      g_byte_array_unref (content);

      cert = g_tls_certificate_get_issuer (cert);
    }

  self->priv->server_cert = g_object_new (IDLE_TYPE_TLS_CERTIFICATE,
      "object-path", cert_object_path,
      "certificate-chain-data", certificates,
      "certificate-type", "x509",
      "dbus-daemon",
        tp_base_connection_get_dbus_daemon (
          tp_base_channel_get_connection (TP_BASE_CHANNEL (self))),
      NULL);
  self->priv->server_cert_path = cert_object_path;
  g_ptr_array_unref (certificates);

  IDLE_DEBUG ("Server TLS channel constructed at %s", path);
}

static void
idle_server_tls_channel_fill_immutable_properties (
    TpBaseChannel *chan,
    GHashTable *properties)
{
  TP_BASE_CHANNEL_CLASS (idle_server_tls_channel_parent_class)
      ->fill_immutable_properties (chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
      TP_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION, "ServerCertificate",
      TP_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION, "Hostname",
      TP_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION, "ReferenceIdentities",
      NULL);
}

static gchar *
idle_server_tls_channel_get_object_path_suffix (TpBaseChannel *base)
{
  static guint count = 0;

  return g_strdup_printf ("ServerTLSChannel%u", ++count);
}

static void
idle_server_tls_channel_init (IdleServerTLSChannel *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      IDLE_TYPE_SERVER_TLS_CHANNEL, IdleServerTLSChannelPrivate);
}

static void
idle_server_tls_channel_class_init (IdleServerTLSChannelClass *klass)
{
  static TpDBusPropertiesMixinPropImpl server_tls_props[] = {
    { "ServerCertificate", "server-certificate", NULL },
    { "Hostname", "hostname", NULL },
    { "ReferenceIdentities", "reference-identities", NULL },
    { NULL }
  };

  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  TpBaseChannelClass *base_class = TP_BASE_CHANNEL_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (IdleServerTLSChannelPrivate));

  oclass->get_property = idle_server_tls_channel_get_property;
  oclass->set_property = idle_server_tls_channel_set_property;
  oclass->dispose = idle_server_tls_channel_dispose;
  oclass->finalize = idle_server_tls_channel_finalize;
  oclass->constructed = idle_server_tls_channel_constructed;

  base_class->channel_type = TP_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION;
  base_class->target_handle_type = TP_HANDLE_TYPE_NONE;
  base_class->fill_immutable_properties =
      idle_server_tls_channel_fill_immutable_properties;
  base_class->get_object_path_suffix =
      idle_server_tls_channel_get_object_path_suffix;
  base_class->close = idle_server_tls_channel_close;

  pspec = g_param_spec_boxed ("server-certificate", "Server certificate path",
      "The object path of the server certificate.",
      DBUS_TYPE_G_OBJECT_PATH,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_SERVER_CERTIFICATE, pspec);

  pspec = g_param_spec_string ("hostname", "The hostname to be verified",
      "The hostname which should be certified by the server certificate.",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_HOSTNAME, pspec);

  pspec = g_param_spec_boxed ("reference-identities",
      "The various identities to check the certificate against",
      "The server certificate identity should match one of these identities.",
      G_TYPE_STRV,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_REFERENCE_IDENTITIES, pspec);

  pspec = g_param_spec_object ("certificate", "The GTLSCertificate",
      "The GTLSCertificate object containing the TLS information",
      G_TYPE_TLS_CERTIFICATE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_CERTIFICATE, pspec);

  tp_dbus_properties_mixin_implement_interface (oclass,
      TP_IFACE_QUARK_CHANNEL_TYPE_SERVER_TLS_CONNECTION,
      tp_dbus_properties_mixin_getter_gobject_properties, NULL,
      server_tls_props);
}

static void
idle_server_tls_channel_close (TpBaseChannel *base)
{
  IDLE_DEBUG ("Close() called on the TLS channel %p", base);
  tp_base_channel_destroyed (base);
}

IdleTLSCertificate *
idle_server_tls_channel_get_certificate (IdleServerTLSChannel *self)
{
  return self->priv->server_cert;
}
