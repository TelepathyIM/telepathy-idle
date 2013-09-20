
"""
Regression test for a bug where, if you were in a IRC channel that had the same
name as your nickname (e.g. user 'foo' in room '#foo'), all private 1:1 messages
to foo would appear to also be coming through room #foo as well (bug #19766)
"""

from idletest import exec_test, BaseIRCServer
from servicetest import EventPattern, call_async, TimeoutError, sync_dbus
from constants import *
import dbus

# same nick and channel
CHANNEL = '#foo'
NICK = 'foo'
REMOTEUSER = 'remoteuser'

group_received_flag = False;
def group_received_cb(id, timestamp, sender, type, flags, text):
    global group_received_flag
    group_received_flag = True

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    # join a chat room with the same name as our nick
    call_async(q, conn.Requests,'CreateChannel',
            { CHANNEL_TYPE: CHANNEL_TYPE_TEXT,
              TARGET_HANDLE_TYPE: HT_ROOM,
              TARGET_ID: CHANNEL })

    # wait for the join to finish
    ret = q.expect('dbus-return', method='CreateChannel')
    muc_path = ret.value
    chan = bus.get_object(conn.bus_name, ret.value[0])
    group_text_chan = dbus.Interface(chan, CHANNEL_TYPE_TEXT)
    group_text_chan.connect_to_signal('Received', group_received_cb)
    q.expect('dbus-signal', signal='MembersChanged')

    stream.sendMessage('PRIVMSG', NICK, ':PRIVATE', prefix=REMOTEUSER)

    event = q.expect('dbus-signal', signal='Received')
    # this seems a bit fragile, but I'm not entirely sure how else to ensure
    # that the message is not delivered to the MUC channel
    assert event.path not in muc_path

    # verify that we didn't receive a 'Received' D-Bus signal on the group text
    # channel
    global group_received_flag
    sync_dbus(bus, q, conn)
    assert group_received_flag == False

    call_async(q, conn, 'Disconnect')
    return True

if __name__ == '__main__':
    exec_test(test, {'account':NICK})

