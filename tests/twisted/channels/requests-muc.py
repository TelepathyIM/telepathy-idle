"""
Test connecting to a IRC channel via the Requests interface
"""

import functools
from idletest import exec_test, BaseIRCServer, sync_stream
from servicetest import (
    EventPattern, call_async, sync_dbus, wrap_channel, assertEquals,
    assertSameSets, assertContains, assertLength,
)
import constants as cs
import dbus

class DelayJoinServer(BaseIRCServer):
    def handleJOIN(self, args):
        # do nothing; wait for the test to call sendJoin().
        return

def build_request(conn, channel_name, use_room):
    rccs = conn.Properties.Get(cs.CONN_IFACE_REQUESTS,
        'RequestableChannelClasses')

    if use_room:
        # We allow TargetHandleType in Room-flavoured requests, but it has to
        # be None if specified.
        assertContains(
            ({ cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT },
             [cs.TARGET_HANDLE_TYPE, cs.ROOM_NAME],
            ), rccs)

        request = {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.ROOM_NAME: '#idletest'
        }
    else:
        assertContains(
            ({ cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
               cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
             },
             [cs.TARGET_HANDLE, cs.TARGET_ID]
            ), rccs)
        request = {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_ID: '#idletest',
        }

    return dbus.Dictionary(request, signature='sv')

def test(q, bus, conn, stream, use_room=False):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    q.expect('dbus-signal', signal='SelfHandleChanged')
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.Get(cs.CONN, 'SelfHandle', dbus_interface=cs.PROPERTIES_IFACE)

    request = build_request(conn, '#idletest', use_room)
    call_async(q, conn.Requests, 'CreateChannel', request)

    # Idle should try to join the channel.
    q.expect('stream-JOIN')

    # Meanwhile, in another application...

    call_async(q, conn, 'EnsureChannel', request,
        dbus_interface=cs.CONN_IFACE_REQUESTS)

    sync_dbus(bus, q, conn)

    # Now the ircd responds:
    stream.sendJoin('#idletest')

    cc, ec = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-return', method='EnsureChannel'),
        )
    nc = q.expect('dbus-signal', signal='NewChannels')

    path, props = cc.value

    assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT
    assertSameSets(
        [cs.CHANNEL_IFACE_GROUP,
         cs.CHANNEL_IFACE_PASSWORD,
         cs.CHANNEL_IFACE_MESSAGES,
         cs.CHANNEL_IFACE_ROOM,
         cs.CHANNEL_IFACE_SUBJECT,
         cs.CHANNEL_IFACE_ROOM_CONFIG,
         cs.CHANNEL_IFACE_DESTROYABLE,
        ], props[cs.INTERFACES])
    assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_ROOM
    assert props[cs.TARGET_ID] == '#idletest'
    assertEquals('#idletest', props[cs.ROOM_NAME])
    assertEquals('', props[cs.ROOM_SERVER])
    assert props[cs.REQUESTED]
    assert props[cs.INITIATOR_HANDLE] == self_handle
    assert props[cs.INITIATOR_ID] == \
        conn.InspectHandles(cs.HT_CONTACT, [self_handle])[0]

    ec_yours, ec_path, ec_props = ec.value
    assert not ec_yours
    assert ec_path == path
    assert ec_props == props

    channels = nc.args[0]
    assert len(channels) == 1
    nc_path, nc_props = channels[0]
    assert nc_path == path
    assert nc_props == props

    # And again?
    ec_ = conn.EnsureChannel(request,
        dbus_interface=cs.CONN_IFACE_REQUESTS)
    assert ec.value == ec_

    chans = conn.Get(cs.CONN_IFACE_REQUESTS, 'Channels',
        dbus_interface=cs.PROPERTIES_IFACE)
    assert len(chans) == 1
    assert chans[0] == (path, props)

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text',
        ['Destroyable', 'Messages'])

    # Put an unacknowledged message into the channel
    stream.sendMessage('PRIVMSG', '#idletest', ':oi oi', prefix='lol')
    q.expect('dbus-signal', signal='MessageReceived', path=path)

    # Make sure Close()ing the channel makes it respawn. This avoids the old
    # bug where empathy-chat crashing booted you out of all your channels.
    patterns = [EventPattern('stream-PART')]
    q.forbid_events(patterns)
    chan.Close()
    q.expect('dbus-signal', signal='Closed', path=chan.object_path)
    e = q.expect('dbus-signal', signal='NewChannels')

    path, props = e.args[0][0]
    assertEquals(chan.object_path, path)
    # We requested the channel originally, but we didn't request it popping
    # back up.
    assertEquals(0, props[cs.INITIATOR_HANDLE])
    assert not props[cs.REQUESTED]

    # The unacknowledged message should still be there and be marked as rescued.
    messages = chan.Properties.Get(cs.CHANNEL_IFACE_MESSAGES, 'PendingMessages')
    assertLength(1, messages)
    assert messages[0][0]['rescued'], messages[0]

    # Check that ensuring a respawned channel does what you'd expect.
    ec_yours, ec_path, ec_props = conn.EnsureChannel(request,
        dbus_interface=cs.CONN_IFACE_REQUESTS)
    assert not ec_yours
    assertEquals(chan.object_path, ec_path)
    assertEquals(props, ec_props)

    sync_stream(q, stream)
    q.unforbid_events(patterns)

    chan.RemoveMembers([self_handle], "bye bye cruel\r\nworld",
        dbus_interface=cs.CHANNEL_IFACE_GROUP)

    part_event = q.expect('stream-PART')

    # This is a regression test for
    # <https://bugs.freedesktop.org/show_bug.cgi?id=34812>, where part messages
    # were not correctly colon-quoted.
    #
    # It is also a regression test for
    # <https://bugs.freedesktop.org/show_bug.cgi?id=34840>, where newlines
    # weren't stripped from part messages. We check that both \r and \n are
    # replaced by harmless spaces.
    assertEquals("bye bye cruel  world", part_event.data[1])

    stream.sendPart('#idletest', stream.nick)

    q.expect('dbus-signal', signal='Closed')

    chans = conn.Get(cs.CONN_IFACE_REQUESTS, 'Channels',
        dbus_interface=cs.PROPERTIES_IFACE)
    assert len(chans) == 0

if __name__ == '__main__':
    exec_test(test, protocol=DelayJoinServer)
    exec_test(functools.partial(test, use_room=True), protocol=DelayJoinServer)

