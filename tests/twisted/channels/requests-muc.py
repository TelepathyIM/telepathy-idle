"""
Test connecting to a IRC channel via the Requests interface
"""

from idletest import exec_test, BaseIRCServer
from servicetest import EventPattern, call_async, sync_dbus, make_channel_proxy
import constants as cs
import dbus

class DelayJoinServer(BaseIRCServer):
    def handleJOIN(self, args):
        # do nothing; wait for the test to call sendJoin().
        return

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    q.expect('dbus-signal', signal='SelfHandleChanged')
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()

    request = dbus.Dictionary({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
        cs.TARGET_ID: '#idletest',
    }, signature='sv')

    call_async(q, conn, 'CreateChannel', request,
        dbus_interface=cs.CONN_IFACE_REQUESTS)

    # Idle should try to join the channel.
    q.expect('stream-JOIN')

    # Meanwhile, in another application...

    call_async(q, conn, 'EnsureChannel', request,
        dbus_interface=cs.CONN_IFACE_REQUESTS)

    sync_dbus(bus, q, conn)

    # Now the ircd responds:
    stream.sendJoin('#idletest')

    cc, ec = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-return', method='EnsureChannel'),
        )
    nc = q.expect('dbus-signal', signal='NewChannels')

    path, props = cc.value

    assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT
    assert sorted(props[cs.INTERFACES]) == \
        sorted([cs.CHANNEL_IFACE_GROUP,
                cs.CHANNEL_IFACE_PASSWORD,
                cs.TP_AWKWARD_PROPERTIES,
               ])
    assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_ROOM
    assert props[cs.TARGET_ID] == '#idletest'
    assert props[cs.TARGET_HANDLE] == \
        conn.RequestHandles(cs.HT_ROOM, ['#idletest'])[0]
    assert props[cs.REQUESTED]
    assert props[cs.INITIATOR_HANDLE] == self_handle
    assert props[cs.INITIATOR_ID] == \
        conn.InspectHandles(cs.HT_CONTACT, [self_handle])[0]

    ec_yours, ec_path, ec_props = ec.value
    assert not ec_yours
    assert ec_path == path
    assert ec_props == props

    channels = nc.args[0]
    assert len(channels) == 1
    nc_path, nc_props = channels[0]
    assert nc_path == path
    assert nc_props == props

    # And again?
    ec_ = conn.EnsureChannel(request,
        dbus_interface=cs.CONN_IFACE_REQUESTS)
    assert ec.value == ec_

    chans = conn.Get(cs.CONN_IFACE_REQUESTS, 'Channels',
        dbus_interface=cs.PROPERTIES_IFACE)
    assert len(chans) == 1
    assert chans[0] == (path, props)

    chan = make_channel_proxy(conn, path, 'Channel')
    chan.RemoveMembers([self_handle], "", dbus_interface=cs.CHANNEL_IFACE_GROUP)

    q.expect('stream-PART')
    stream.sendPart('#idletest', stream.nick)

    q.expect('dbus-signal', signal='Closed')

    chans = conn.Get(cs.CONN_IFACE_REQUESTS, 'Channels',
        dbus_interface=cs.PROPERTIES_IFACE)
    assert len(chans) == 0

if __name__ == '__main__':
    exec_test(test, protocol=DelayJoinServer)

