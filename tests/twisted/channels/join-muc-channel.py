
"""
Test connecting to a IRC channel
"""

from idletest import exec_test
from servicetest import EventPattern, call_async
from constants import *
import dbus

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    q.expect('dbus-signal', signal='SelfHandleChanged',
        args=[1L])
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    room_handles = conn.RequestHandles(HT_ROOM, ['#idletest'])
    call_async(q, conn, 'RequestChannel', CHANNEL_TYPE_TEXT, HT_ROOM, room_handles[0], True)
    event = q.expect('dbus-return', method='RequestChannel')
    obj_path = event.value[0]

    event = q.expect('dbus-signal', signal='NewChannels')
    channel_details = event.args[0]
    assert len(channel_details) == 1
    path, props = channel_details[0]
    assert path == obj_path
    assert props[TARGET_HANDLE_TYPE] == HT_ROOM
    assert props[TARGET_HANDLE] == room_handles[0]
    assert props[CHANNEL_TYPE] == CHANNEL_TYPE_TEXT

    event = q.expect('dbus-signal', signal='NewChannel')
    assert event.args[0] == obj_path
    assert event.args[1] == CHANNEL_TYPE_TEXT
    assert event.args[2] == HT_ROOM
    assert event.args[3] == room_handles[0]

    q.expect('dbus-signal', signal='MembersChanged')
    call_async(q, conn, 'Disconnect')
    q.expect_many(
            EventPattern('dbus-return', method='Disconnect'),
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]))
    return True

if __name__ == '__main__':
    exec_test(test)

