
"""
Test that messages that are sent within the same message delivery timeout
(default 2s) get delivered in the proper order.  This bug was fixed in rev
8fae4404798, this is just a regression test to ensure that it stays fixed
"""

from idletest import exec_test
from servicetest import EventPattern, call_async
import dbus

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    CHANNEL_NAME = '#idletest'
    room_handles = conn.RequestHandles(2, [CHANNEL_NAME])
    call_async(q, conn, 'RequestChannel', 'org.freedesktop.Telepathy.Channel.Type.Text', 2, room_handles[0], True)

    ret = q.expect('dbus-return', method='RequestChannel')
    q.expect('dbus-signal', signal='MembersChanged')
    chan = bus.get_object(conn.bus_name, ret.value[0])

    text_chan = dbus.Interface(chan,
        u'org.freedesktop.Telepathy.Channel.Type.Text')
    # send a whole bunch of messages in a row
    call_async(q, text_chan, 'Send', 0, '0')
    call_async(q, text_chan, 'Send', 0, '1')
    call_async(q, text_chan, 'Send', 0, '2')
    call_async(q, text_chan, 'Send', 0, '3')
    call_async(q, text_chan, 'Send', 0, '4')

    q.expect('irc-privmsg', data={'message':'0','recipient':CHANNEL_NAME})
    q.expect('irc-privmsg', data={'message':'1','recipient':CHANNEL_NAME})
    q.expect('irc-privmsg', data={'message':'2','recipient':CHANNEL_NAME})
    q.expect('irc-privmsg', data={'message':'3','recipient':CHANNEL_NAME})
    q.expect('irc-privmsg', data={'message':'4','recipient':CHANNEL_NAME})

    call_async(q, conn, 'Disconnect')
    return True

if __name__ == '__main__':
    exec_test(test)

