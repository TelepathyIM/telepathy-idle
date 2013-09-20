
"""
Test getting a room-list channel
"""

from idletest import exec_test, BaseIRCServer
from servicetest import EventPattern, call_async, tp_name_prefix, tp_path_prefix, assertEquals
import dbus
import constants as cs

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
                assert r[1] == info['members'] and r[2] == info['subject'] and r[0] == info['handle-name']
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

    call_async(q, conn, 'CreateChannel',
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_ROOM_LIST },
        dbus_interface=cs.CONN_IFACE_REQUESTS)
    ret = q.expect('dbus-return', method='CreateChannel')
    path, properties = ret.value
    assertEquals(cs.CHANNEL_TYPE_ROOM_LIST, properties[cs.CHANNEL_TYPE])

    def looks_like_a_room_list(event):
        channels, = event.args
        if len(channels) != 1:
            return False
        path, props = channels[0]

        return props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_ROOM_LIST and \
            props[cs.TARGET_HANDLE_TYPE] == cs.HT_NONE and \
            props[cs.TARGET_ID] == ''

    e = q.expect('dbus-signal', signal='NewChannels',
        predicate=looks_like_a_room_list)

    chan = bus.get_object(conn.bus_name, path)
    list_chan = dbus.Interface(chan, cs.CHANNEL_TYPE_ROOM_LIST)
    list_chan.ListRooms();
    q.expect('dbus-signal', signal='GotRooms', predicate=lambda x:check_rooms(x.args[0]))

    call_async(q, conn, 'Disconnect')
    q.expect_many(
            EventPattern('dbus-return', method='Disconnect'),
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]))
    return True

if __name__ == '__main__':
    exec_test(test, protocol=RoomListServer)

