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

#ifndef __TELEPATHY_ERRORS_H__
#define __TELEPATHY_ERRORS_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum
{
	ChannelBanned,
	ChannelFull,
	ChannelInviteOnly,
  InvalidHandle,    /** The contact name specified is unknown on this channel
                     *  or connection.
                     */
  Disconnected,     /** The connection is not currently connected and cannot be
                     *  used.
                     */
  InvalidArgument,  /** Raised when one of the provided arguments is invalid.
                     */
  NetworkError,     /** Raised when there is an error reading from or writing
                     *  to the network.
                     */
  PermissionDenied, /** The user is not permitted to perform the requested
                     *  operation.
                     */
  NotAvailable,     /** Raised when the requested functionality is temporarily
                     *  unavailable.
                     */
  NotImplemented,   /** Raised when the requested method, channel, etc is not
                     *  available on this connection.
                     */
} TelepathyErrors; 

GQuark telepathy_errors_quark (void);
#define TELEPATHY_ERRORS telepathy_errors_quark ()

G_END_DECLS

#endif /* #ifndef __TELEPATHY_ERRORS_H__*/
