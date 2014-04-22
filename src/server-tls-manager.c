/*
 * server-tls-manager.c - Source for IdleServerTLSManager
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
#include "server-tls-manager.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#define IDLE_DEBUG_FLAG IDLE_DEBUG_TLS
#include "idle-debug.h"
#include "idle-connection.h"
#include "server-tls-channel.h"

static void channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (IdleServerTLSManager, idle_server_tls_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init));

enum {
  PROP_CONNECTION = 1,
  NUM_PROPERTIES
};

struct _IdleServerTLSManagerPrivate {
  /* Properties */
  IdleConnection *connection;

  /* Current operation data */
  IdleServerTLSChannel *channel;
  GSimpleAsyncResult *async_result;

  /* List of owned TpBaseChannel not yet closed by the client */
  GList *completed_channels;

  gboolean dispose_has_run;
};

#define chainup ((WockyTLSHandlerClass *) \
    idle_server_tls_manager_parent_class)

static void
idle_server_tls_manager_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  IdleServerTLSManager *self = IDLE_SERVER_TLS_MANAGER (object);

  switch (property_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, self->priv->connection);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
idle_server_tls_manager_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  IdleServerTLSManager *self = IDLE_SERVER_TLS_MANAGER (object);

  switch (property_id)
    {
    case PROP_CONNECTION:
      self->priv->connection = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
close_all (IdleServerTLSManager *self)
{
  GList *l;

  if (self->priv->channel != NULL)
    tp_base_channel_close (TP_BASE_CHANNEL (self->priv->channel));

  l = self->priv->completed_channels;
  while (l != NULL)
    {
      /* use a temporary variable as the ::closed callback will delete
       * the link from the list. */
      GList *next = l->next;

      tp_base_channel_close (l->data);

      l = next;
    }
}

static void
connection_status_changed_cb (IdleConnection *conn,
    guint status,
    guint reason,
    gpointer user_data)
{
  IdleServerTLSManager *self = user_data;

  IDLE_DEBUG ("Connection status changed, now %d", status);

  if (status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      close_all (self);
      tp_clear_object (&self->priv->connection);
    }
}

static void
complete_verify (IdleServerTLSManager *self)
{
  /* Move channel to a list until a client Close() it */
  if (self->priv->channel != NULL)
    {
      self->priv->completed_channels = g_list_prepend (
          self->priv->completed_channels,
          g_object_ref (self->priv->channel));
    }

  g_simple_async_result_complete (self->priv->async_result);

  /* Reset to initial state */
  g_clear_object (&self->priv->channel);
  g_clear_object (&self->priv->async_result);
}

static void
server_tls_channel_closed_cb (IdleServerTLSChannel *channel,
    gpointer user_data)
{
  IdleServerTLSManager *self = user_data;

  IDLE_DEBUG ("Server TLS channel closed.");

  if (channel == self->priv->channel)
    {
      IDLE_DEBUG ("Channel closed before being handled. Failing verification");

      g_simple_async_result_set_error (self->priv->async_result,
          IDLE_SERVER_TLS_ERROR, 0, "TLS verification channel closed");

      self->priv->channel = NULL;
      complete_verify (self);
    }
  else
    {
      GList *l;

      l = g_list_find (self->priv->completed_channels, channel);
      g_assert (l != NULL);

      self->priv->completed_channels = g_list_delete_link (
          self->priv->completed_channels, l);
    }

  tp_channel_manager_emit_channel_closed_for_object (TP_CHANNEL_MANAGER (self),
      TP_BASE_CHANNEL (channel));
  g_object_unref (channel);
}

GQuark
idle_server_tls_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("server-tls-error");

  return quark;
}

static void
tls_certificate_accepted_cb (IdleTLSCertificate *certificate,
    gpointer user_data)
{
  IdleServerTLSManager *self = user_data;

  IDLE_DEBUG ("TLS certificate accepted");

  complete_verify (self);
}

static void
tls_certificate_rejected_cb (IdleTLSCertificate *certificate,
    GPtrArray *rejections,
    gpointer user_data)
{
  IdleServerTLSManager *self = user_data;

  IDLE_DEBUG ("TLS certificate rejected with rejections %p, length %u.",
      rejections, rejections->len);

  g_simple_async_result_set_error (self->priv->async_result,
      IDLE_SERVER_TLS_ERROR, 0, "TLS certificate rejected");

  complete_verify (self);
}

void
idle_server_tls_manager_verify_async (IdleServerTLSManager *self,
    GTlsCertificate *certificate,
    const gchar *peername,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  IdleTLSCertificate *cert;
  GSimpleAsyncResult *result;
  const gchar *identities[] = { peername, NULL };

  g_return_if_fail (self->priv->async_result == NULL);

  IDLE_DEBUG ("verify_async() called on the IdleServerTLSManager.");

  result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, idle_server_tls_manager_verify_async);

  if (self->priv->connection == NULL)
    {
      IDLE_DEBUG ("connection already went away; failing immediately");
      g_simple_async_result_set_error (result, TP_ERROR, TP_ERROR_CANCELLED,
          "The Telepathy connection has already been disconnected");
      g_simple_async_result_complete_in_idle (result);
      g_object_unref (result);
      return;
    }

  self->priv->async_result = result;

  self->priv->channel = g_object_new (IDLE_TYPE_SERVER_TLS_CHANNEL,
      "connection", self->priv->connection,
      "certificate", certificate,
      "hostname", peername,
      "reference-identities", identities,
      NULL);

  g_signal_connect (self->priv->channel, "closed",
      G_CALLBACK (server_tls_channel_closed_cb), self);

  cert = idle_server_tls_channel_get_certificate (self->priv->channel);

  g_signal_connect (cert, "accepted",
      G_CALLBACK (tls_certificate_accepted_cb), self);
  g_signal_connect (cert, "rejected",
      G_CALLBACK (tls_certificate_rejected_cb), self);

  /* emit NewChannel on the ChannelManager iface */
  tp_channel_manager_emit_new_channel (TP_CHANNEL_MANAGER (self),
      (TpBaseChannel *) self->priv->channel, NULL);
}

gboolean
idle_server_tls_manager_verify_finish (IdleServerTLSManager *self,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (
      G_SIMPLE_ASYNC_RESULT (result), error))
    return FALSE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
    G_OBJECT(self), idle_server_tls_manager_verify_async), FALSE);
  return TRUE;
}

static void
idle_server_tls_manager_init (IdleServerTLSManager *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      IDLE_TYPE_SERVER_TLS_MANAGER, IdleServerTLSManagerPrivate);
}

static void
idle_server_tls_manager_dispose (GObject *object)
{
  IdleServerTLSManager *self = IDLE_SERVER_TLS_MANAGER (object);

  IDLE_DEBUG ("%p", self);

  if (self->priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  tp_clear_object (&self->priv->connection);

  G_OBJECT_CLASS (idle_server_tls_manager_parent_class)->dispose (object);
}

static void
idle_server_tls_manager_finalize (GObject *object)
{
  IdleServerTLSManager *self = IDLE_SERVER_TLS_MANAGER (object);

  IDLE_DEBUG ("%p", self);

  close_all (self);

  G_OBJECT_CLASS (idle_server_tls_manager_parent_class)->finalize (object);
}

static void
idle_server_tls_manager_constructed (GObject *object)
{
  IdleServerTLSManager *self = IDLE_SERVER_TLS_MANAGER (object);
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (idle_server_tls_manager_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (object);

  IDLE_DEBUG ("Server TLS Manager constructed");

  tp_g_signal_connect_object (self->priv->connection, "status-changed",
      G_CALLBACK (connection_status_changed_cb), object, 0);
}

static void
idle_server_tls_manager_class_init (IdleServerTLSManagerClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (IdleServerTLSManagerPrivate));

  oclass->dispose = idle_server_tls_manager_dispose;
  oclass->finalize = idle_server_tls_manager_finalize;
  oclass->constructed = idle_server_tls_manager_constructed;
  oclass->set_property = idle_server_tls_manager_set_property;
  oclass->get_property = idle_server_tls_manager_get_property;

  pspec = g_param_spec_object ("connection", "Base connection object",
      "base connection object that owns this manager.",
      TP_TYPE_BASE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_CONNECTION, pspec);
}

static void
idle_server_tls_manager_foreach_channel (TpChannelManager *manager,
    TpBaseChannelFunc func,
    gpointer user_data)
{
  IdleServerTLSManager *self = IDLE_SERVER_TLS_MANAGER (manager);
  GList *l;

  if (self->priv->channel != NULL)
    func (TP_BASE_CHANNEL (self->priv->channel), user_data);

  for (l = self->priv->completed_channels; l != NULL; l = l->next)
    {
      func (l->data, user_data);
    }
}

static void
channel_manager_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = idle_server_tls_manager_foreach_channel;

  /* these channels are not requestable. */
  iface->ensure_channel = NULL;
  iface->create_channel = NULL;
  iface->foreach_channel_class = NULL;
}

static TpConnectionStatusReason
cert_reject_reason_to_conn_reason (TpTLSCertificateRejectReason tls_reason)
{
  #define EASY_CASE(x) \
    case TP_TLS_CERTIFICATE_REJECT_REASON_ ## x: \
      return TP_CONNECTION_STATUS_REASON_CERT_ ## x;

  switch (tls_reason)
    {
      EASY_CASE (UNTRUSTED);
      EASY_CASE (EXPIRED);
      EASY_CASE (NOT_ACTIVATED);
      EASY_CASE (FINGERPRINT_MISMATCH);
      EASY_CASE (HOSTNAME_MISMATCH);
      EASY_CASE (SELF_SIGNED);
      EASY_CASE (REVOKED);
      EASY_CASE (INSECURE);
      EASY_CASE (LIMIT_EXCEEDED);

      case TP_TLS_CERTIFICATE_REJECT_REASON_UNKNOWN:
      default:
        return TP_CONNECTION_STATUS_REASON_CERT_OTHER_ERROR;
    }

  #undef EASY_CASE
}

void
idle_server_tls_manager_get_rejection_details (IdleServerTLSManager *self,
    gchar **dbus_error,
    GHashTable **details,
    TpConnectionStatusReason *reason)
{
  IdleTLSCertificate *certificate;
  GPtrArray *rejections;
  GValueArray *rejection;
  TpTLSCertificateRejectReason tls_reason;

  /* We probably want the rejection details of last completed operation */
  g_return_if_fail (self->priv->completed_channels != NULL);

  certificate = idle_server_tls_channel_get_certificate (
      self->priv->completed_channels->data);
  g_object_get (certificate,
      "rejections", &rejections,
      NULL);

  /* we return 'Invalid_Argument' if Reject() is called with zero
   * reasons, so if this fails something bad happened.
   */
  g_assert (rejections->len >= 1);

  rejection = g_ptr_array_index (rejections, 0);

  tls_reason = g_value_get_uint (g_value_array_get_nth (rejection, 0));
  *dbus_error = g_value_dup_string (g_value_array_get_nth (rejection, 1));
  *details = g_value_dup_boxed (g_value_array_get_nth (rejection, 2));

  *reason = cert_reject_reason_to_conn_reason (tls_reason);

  tp_clear_boxed (TP_ARRAY_TYPE_TLS_CERTIFICATE_REJECTION_LIST,
      &rejections);
}
