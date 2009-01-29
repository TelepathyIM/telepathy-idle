
"""
Test getting multiple room-list channels.  telepathy-idle internally only
creates a single roomlist channel (since there's not really any reason to have
more than one) and passes that channel out whenever somebody asks for it.

This test just excercises the case where we've already created the channel and
we just need to hand it out to the next requestor
"""

from idletest import exec_test, BaseIRCServer
from servicetest import EventPattern, call_async, tp_name_prefix, tp_path_prefix
import dbus

HANDLE_TYPE_NONE=0

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    q.expect('dbus-signal', signal='SelfHandleChanged',
        args=[1L])

    # request a roomlist channel
    EXPECTED_ROOM = tp_path_prefix + '/Connection/idle/irc/test_40localhost/RoomListChannel0'
    call_async(q, conn, 'RequestChannel',
            tp_name_prefix + '.Channel.Type.RoomList', HANDLE_TYPE_NONE,
            0, True)
    ret = q.expect('dbus-return', method='RequestChannel')
    assert EXPECTED_ROOM == ret.value[0]

    # verify that a new channel was created and signalled
    q.expect('dbus-signal', signal='NewChannels',
            args=[[(EXPECTED_ROOM,
                { tp_name_prefix + u'.Channel.ChannelType':
                    tp_name_prefix + u'.Channel.Type.RoomList',
                    tp_name_prefix + u'.Channel.TargetHandle': 0,
                    tp_name_prefix + u'.Channel.TargetHandleType': 0})]])
    q.expect('dbus-signal', signal='NewChannel',
            args=[tp_path_prefix + '/Connection/idle/irc/test_40localhost/RoomListChannel0',
                tp_name_prefix + u'.Channel.Type.RoomList', 0, 0, 1])

    # try to request another roomlist channel
    call_async(q, conn, 'RequestChannel',
            tp_name_prefix + '.Channel.Type.RoomList', HANDLE_TYPE_NONE,
            0, True)
    ret = q.expect('dbus-return', method='RequestChannel')
    # assert that it returns the same channel
    assert EXPECTED_ROOM == ret.value[0]

    call_async(q, conn, 'Disconnect')
    q.expect_many(
            EventPattern('dbus-return', method='Disconnect'),
            EventPattern('dbus-signal', signal='ChannelClosed', args=[EXPECTED_ROOM]),
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]))
    return True

if __name__ == '__main__':
    exec_test(test)

