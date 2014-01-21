
"""
Test getting multiple room-list channels.  telepathy-idle internally only
creates a single roomlist channel (since there's not really any reason to have
more than one) and passes that channel out whenever somebody asks for it.

This test just excercises the case where we've already created the channel and
we just need to hand it out to the next requestor
"""

from idletest import exec_test
from servicetest import EventPattern, call_async, assertEquals
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    q.expect('dbus-signal', signal='SelfContactChanged',
        args=[1L, 'test'])

    # request a roomlist channel
    call_async(q, conn, 'CreateChannel',
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_ROOM_LIST },
        dbus_interface=cs.CONN_IFACE_REQUESTS)
    ret = q.expect('dbus-return', method='CreateChannel')
    path, properties = ret.value
    assertEquals(cs.CHANNEL_TYPE_ROOM_LIST, properties[cs.CHANNEL_TYPE])

    # verify that a new channel was created and signalled
    def looks_like_a_room_list(event):
        path, props = event.args

        return props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_ROOM_LIST and \
            props[cs.TARGET_HANDLE_TYPE] == cs.HT_NONE and \
            props[cs.TARGET_ID] == ''

    q.expect('dbus-signal', signal='NewChannel',
        predicate=looks_like_a_room_list)

    # FIXME: this is pretty questionable.

    # try to request another roomlist channel
    call_async(q, conn, 'EnsureChannel',
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_ROOM_LIST },
        dbus_interface=cs.CONN_IFACE_REQUESTS)
    ret = q.expect('dbus-return', method='EnsureChannel')
    yours, path2, properties2 = ret.value

    # assert that it returns the same channel
    assertEquals(path, path2)
    assert not yours

    call_async(q, conn, 'Disconnect')
    q.expect_many(
            EventPattern('dbus-return', method='Disconnect'),
            EventPattern('dbus-signal', signal='ChannelClosed', args=[path]),
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]))
    return True

if __name__ == '__main__':
    exec_test(test)

