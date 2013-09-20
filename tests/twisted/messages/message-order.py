
"""
Test that messages that are sent within the same message delivery timeout
(default 2s) get delivered in the proper order.  This bug was fixed in rev
8fae4404798, this is just a regression test to ensure that it stays fixed
"""

from idletest import exec_test
from servicetest import EventPattern, call_async, assertEquals
from constants import *
import dbus

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    CHANNEL_NAME = '#idletest'
    call_async(q, conn.Requests, 'CreateChannel',
            { CHANNEL_TYPE: CHANNEL_TYPE_TEXT,
              TARGET_HANDLE_TYPE: HT_ROOM,
              TARGET_ID: CHANNEL_NAME })

    ret = q.expect('dbus-return', method='CreateChannel')
    q.expect('dbus-signal', signal='MembersChanged')
    chan = bus.get_object(conn.bus_name, ret.value[0])

    text_chan = dbus.Interface(chan, CHANNEL_TYPE_TEXT)

    # send a whole bunch of messages in a row and make sure they get delivered
    # in the proper order
    NUM_MESSAGES = 4
    for i in range(NUM_MESSAGES):
        message = [
            {'message-type': MT_NORMAL },
            {'content-type': 'text/plain',
             'content': str(i) }]

        call_async(q, text_chan, 'SendMessage', message, 0)

    for i in range(NUM_MESSAGES):
        message = q.expect('stream-PRIVMSG')
        assertEquals([CHANNEL_NAME, str(i)], message.data)

    call_async(q, conn, 'Disconnect')

if __name__ == '__main__':
    exec_test(test)

