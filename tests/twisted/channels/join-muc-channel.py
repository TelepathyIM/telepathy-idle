
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
    q.expect('dbus-signal', signal='SelfContactChanged',
        args=[1L, 'test'])
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    call_async(q, conn.Requests, 'CreateChannel',
            { CHANNEL_TYPE: CHANNEL_TYPE_TEXT,
              TARGET_HANDLE_TYPE: HT_ROOM,
              TARGET_ID: '#idletest' })

    q.expect('stream-JOIN')
    event = q.expect('dbus-return', method='CreateChannel')
    obj_path = event.value[0]

    pattern = EventPattern('dbus-signal', signal='NewChannels')
    event = q.expect_many(pattern)[0]
    q.forbid_events([pattern])
    channel_details = event.args[0]
    assert len(channel_details) == 1
    path, props = channel_details[0]
    assert path == obj_path
    assert props[TARGET_HANDLE_TYPE] == HT_ROOM
    assert props[CHANNEL_TYPE] == CHANNEL_TYPE_TEXT

    q.expect('dbus-signal', signal='MembersChanged')
    call_async(q, conn, 'Disconnect')
    q.expect_many(
            EventPattern('dbus-return', method='Disconnect'),
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]))
    return True

if __name__ == '__main__':
    exec_test(test)

