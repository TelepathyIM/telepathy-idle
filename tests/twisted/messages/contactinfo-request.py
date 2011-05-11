
"""
Test RequestContactInfo implementation
"""

from idletest import exec_test
from servicetest import EventPattern, assertEquals, call_async
from constants import *
import dbus

CHANNEL_NAMES = ['#idletest0', '#idletest1']

def validate_vcard(vcard):
    channel_names = []
    for (name, parameters, value) in vcard:
        if name == 'fn':
            assertEquals('Test User', value[0])
        elif name == 'x-irc-channel':
            channel_names.append(value[0])
        elif name == 'x-irc-server':
            assertEquals('idle.test.server', value[0])
            assertEquals('Idle Test Server', value[1])
        elif name == 'x-host':
            assertEquals('localhost', value[0])
        elif name == 'x-idle-time':
            assertEquals('42', value[0]) # fake value
        elif name == 'nickname':
            assertEquals('test', value[0])
        elif name == 'x-presence-type':
            assertEquals(PRESENCE_AVAILABLE, int(value[0]))
        elif name == 'x-presence-status-identifier':
            assertEquals('available', value[0])
    assertEquals(CHANNEL_NAMES, channel_names)

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    q.expect('dbus-signal', signal='SelfHandleChanged',
        args=[1L])
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()

    room_handles = conn.RequestHandles(HT_ROOM, CHANNEL_NAMES)
    for room_handle in room_handles:
        call_async(q, conn, 'RequestChannel', CHANNEL_TYPE_TEXT, HT_ROOM, room_handle, True)
        q.expect('dbus-return', method='RequestChannel')
        q.expect('dbus-signal', signal='NewChannels')

    contact_info = dbus.Interface(conn, CONN_IFACE_CONTACT_INFO)

    # The test server alternatively returns success and RPL_TRYAGAIN in
    # response to WHOIS queries. The first WHOIS query is issued as part of the
    # handshake when the connection is initially made. So this one is going to
    # return a RPL_TRYAGAIN resulting in a SERVICE_BUSY error.
    call_async(q, contact_info, 'RequestContactInfo', self_handle)
    event = q.expect('dbus-error', method='RequestContactInfo')
    assertEquals(SERVICE_BUSY, event.name)

    call_async(q, contact_info, 'RequestContactInfo', self_handle)
    event = q.expect('dbus-return', method='RequestContactInfo')
    vcard = event.value[0]

    validate_vcard(vcard)

    call_async(q, conn, 'Disconnect')
    q.expect_many(
            EventPattern('dbus-return', method='Disconnect'),
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]))
    return True

if __name__ == '__main__':
    exec_test(test)
