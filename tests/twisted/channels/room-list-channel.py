
"""
Test getting a room-list channel
"""

from idletest import exec_test, BaseIRCServer
from servicetest import EventPattern, call_async, tp_name_prefix, tp_path_prefix
import dbus

HANDLE_TYPE_NONE=0

TEST_CHANNELS = (
        ('#foo', 4, 'discussion about foo'),
        ('#bar', 8, 'discussion about bar'),
        ('#baz', 230, '')  #empty topic
        )

def check_rooms(received_rooms):
    for room in received_rooms:
        assert room[1] == tp_name_prefix + '.Channel.Type.Text'
        info = room[2]
        found = False
        for r in TEST_CHANNELS:
            if r[0] == info['name']:
                found = True
                assert r[1] == info['members'] and r[2] == info['subject']
                break;
        assert found
    return True


class RoomListServer(BaseIRCServer):
    def handleLIST(self, args, prefix):
        for chan in TEST_CHANNELS:
            self.sendMessage('322', '%s %s %d :%s' % (self.nick, chan[0], chan[1], chan[2]),
                    prefix="idle.test.server")
        self.sendMessage('323', '%s :End of /LIST' % self.nick, prefix="idle.test.server")
        pass

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    q.expect('dbus-signal', signal='SelfHandleChanged',
        args=[1L])

    call_async(q, conn, 'RequestChannel',
            tp_name_prefix + '.Channel.Type.RoomList', HANDLE_TYPE_NONE,
            0, True)
    ret = q.expect('dbus-return', method='RequestChannel')
    assert 'RoomListChannel' in ret.value[0]

    q.expect('dbus-signal', signal='NewChannels',
            args=[[(tp_path_prefix + '/Connection/idle/irc/test_40localhost/RoomListChannel0',
                { tp_name_prefix + u'.Channel.ChannelType':
                    tp_name_prefix + u'.Channel.Type.RoomList',
                    tp_name_prefix + u'.Channel.TargetHandle': 0,
                    tp_name_prefix + u'.Channel.TargetHandleType': 0})]])
    q.expect('dbus-signal', signal='NewChannel',
            args=[tp_path_prefix + '/Connection/idle/irc/test_40localhost/RoomListChannel0',
                tp_name_prefix + u'.Channel.Type.RoomList', 0, 0, 1])
    chan = bus.get_object(conn.bus_name,
            tp_path_prefix + '/Connection/idle/irc/test_40localhost/RoomListChannel0')
    list_chan = dbus.Interface(chan, tp_name_prefix + u'.Channel.Type.RoomList')
    list_chan.ListRooms();
    q.expect('dbus-signal', signal='GotRooms', predicate=lambda x:check_rooms(x.args[0]))

    call_async(q, conn, 'Disconnect')
    q.expect_many(
            EventPattern('dbus-return', method='Disconnect'),
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]))
    return True

if __name__ == '__main__':
    exec_test(test, protocol=RoomListServer)

