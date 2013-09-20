
"""
Test connecting to a IRC channel when prompted by a bouncer
"""

from idletest import exec_test
from servicetest import (
    EventPattern, assertEquals, call_async, make_channel_proxy
)
from constants import *

def test_join_bouncer(q, conn, stream, room):
    stream.sendJoin(room)

    new_channels = EventPattern('dbus-signal', signal='NewChannels')
    event = q.expect_many(new_channels)[0]
    q.forbid_events([new_channels])
    channel_details = event.args[0]
    assertEquals(1, len(channel_details))
    path, props = channel_details[0]
    assertEquals(HT_ROOM, props[TARGET_HANDLE_TYPE])
    assertEquals(CHANNEL_TYPE_TEXT, props[CHANNEL_TYPE])

    q.expect('dbus-signal', signal='MembersChanged')

    q.unforbid_events([new_channels])
    return path

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    q.expect('dbus-signal', signal='SelfHandleChanged',
        args=[1L])
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    CHANNEL_NAME = "#idletest"

    self_handle = conn.GetSelfHandle()

    # The bouncer initiates a JOIN.
    path = test_join_bouncer(q, conn, stream, CHANNEL_NAME)

    # We PART.
    chan = make_channel_proxy(conn, path, 'Channel')
    chan.RemoveMembers([self_handle], "bye bye cruel world",
        dbus_interface=CHANNEL_IFACE_GROUP)
    q.expect('dbus-signal', signal='MembersChanged')

    # The bouncer initiates a JOIN to force the issue.
    test_join_bouncer(q, conn, stream, CHANNEL_NAME)

    call_async(q, conn, 'Disconnect')
    q.expect_many(
            EventPattern('dbus-return', method='Disconnect'),
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]))
    return True

if __name__ == '__main__':
    exec_test(test)
