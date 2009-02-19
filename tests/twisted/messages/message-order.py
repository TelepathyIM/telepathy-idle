
"""
Test that messages that are sent within the same message delivery timeout
(default 2s) get delivered in the proper order.  This bug was fixed in rev
8fae4404798, this is just a regression test to ensure that it stays fixed
"""

from idletest import exec_test
from servicetest import EventPattern, call_async
from constants import *
import dbus

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    CHANNEL_NAME = '#idletest'
    room_handles = conn.RequestHandles(HT_ROOM, [CHANNEL_NAME])
    call_async(q, conn, 'RequestChannel', CHANNEL_TYPE_TEXT, HT_ROOM,
            room_handles[0], True)

    ret = q.expect('dbus-return', method='RequestChannel')
    q.expect('dbus-signal', signal='MembersChanged')
    chan = bus.get_object(conn.bus_name, ret.value[0])

    text_chan = dbus.Interface(chan, CHANNEL_TYPE_TEXT)

    # send a whole bunch of messages in a row and make sure they get delivered
    # in the proper order
    NUM_MESSAGES = 4
    for i in range(NUM_MESSAGES):
        call_async(q, text_chan, 'Send', 0, str(i))

    for i in range(NUM_MESSAGES):
        message = q.expect('stream-PRIVMSG')
        assert message.data[0] == CHANNEL_NAME
        assert message.data[1].rstrip('\r\n') == str(i)

    call_async(q, conn, 'Disconnect')
    return True

if __name__ == '__main__':
    exec_test(test)

