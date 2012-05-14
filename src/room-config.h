/*
 * room-config.h - header for Channel.I.RoomConfig1 implementation
 * Copyright © 2011-2012 Collabora Ltd.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef IDLE_ROOM_CONFIG_H
#define IDLE_ROOM_CONFIG_H

#include <glib-object.h>
#include <telepathy-glib/base-room-config.h>

typedef struct _IdleRoomConfig IdleRoomConfig;
typedef struct _IdleRoomConfigClass IdleRoomConfigClass;
typedef struct _IdleRoomConfigPrivate IdleRoomConfigPrivate;

struct _IdleRoomConfigClass {
    TpBaseRoomConfigClass parent_class;
};

struct _IdleRoomConfig {
    TpBaseRoomConfig parent;

    IdleRoomConfigPrivate *priv;
};

IdleRoomConfig *idle_room_config_new (
    TpBaseChannel *channel);

/* TYPE MACROS */
GType idle_room_config_get_type (void);

#define IDLE_TYPE_ROOM_CONFIG \
  (idle_room_config_get_type ())
#define IDLE_ROOM_CONFIG(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), IDLE_TYPE_ROOM_CONFIG, IdleRoomConfig))
#define IDLE_ROOM_CONFIG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), IDLE_TYPE_ROOM_CONFIG,\
                           IdleRoomConfigClass))
#define IDLE_IS_ROOM_CONFIG(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), IDLE_TYPE_ROOM_CONFIG))
#define IDLE_IS_ROOM_CONFIG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), IDLE_TYPE_ROOM_CONFIG))
#define IDLE_ROOM_CONFIG_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), IDLE_TYPE_ROOM_CONFIG, \
                              IdleRoomConfigClass))

#endif /* IDLE_ROOM_CONFIG_H */
