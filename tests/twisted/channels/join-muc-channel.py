
"""
Test connecting to a IRC channel
"""

from idletest import exec_test
from servicetest import EventPattern, call_async
import dbus

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    q.expect('dbus-signal', signal='SelfHandleChanged',
        args=[1L])
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    room_handles = conn.RequestHandles(2, ['#idletest'])
    call_async(q, conn, 'RequestChannel', 'org.freedesktop.Telepathy.Channel.Type.Text', 2, room_handles[0], True)
    q.expect('dbus-return', method='RequestChannel',
            value=('/org/freedesktop/Telepathy/Connection/idle/irc/test_40localhost/MucChannel%d' % room_handles[0],))
    q.expect('dbus-signal', signal='NewChannels',
            args=[[('/org/freedesktop/Telepathy/Connection/idle/irc/test_40localhost/MucChannel%d' % room_handles[0],
                { u'org.freedesktop.Telepathy.Channel.ChannelType':
                    u'org.freedesktop.Telepathy.Channel.Type.Text',
                    u'org.freedesktop.Telepathy.Channel.TargetHandle': room_handles[0],
                    u'org.freedesktop.Telepathy.Channel.TargetHandleType': 2L})]])
    q.expect('dbus-signal', signal='NewChannel',
            args=['/org/freedesktop/Telepathy/Connection/idle/irc/test_40localhost/MucChannel%d' % room_handles[0],
                u'org.freedesktop.Telepathy.Channel.Type.Text', 2L, room_handles[0], 1])
    q.expect('dbus-signal', signal='MembersChanged')
    call_async(q, conn, 'Disconnect')
    q.expect_many(
            EventPattern('dbus-return', method='Disconnect'),
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]))
    return True

if __name__ == '__main__':
    exec_test(test, timeout=10)

