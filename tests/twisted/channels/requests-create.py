
"""
Test creating a channel with the Requests interface
Most of this test was borrowed from a gabble test and modified to fit idle
"""

from idletest import exec_test
from servicetest import EventPattern, call_async, tp_name_prefix
import dbus

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0,1])

    nick = 'foo'
    call_async(q, conn, 'RequestHandles', 1, [nick])
    event = q.expect('dbus-return', method='RequestHandles')
    foo_handle = event.value[0][0]

    properties = conn.GetAll(
            'org.freedesktop.Telepathy.Connection.Interface.Requests',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert properties.get('Channels') == [], properties['Channels']
    assert ({'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Text',
             'org.freedesktop.Telepathy.Channel.TargetHandleType': 1,
             },
             ['org.freedesktop.Telepathy.Channel.TargetHandle',
              'org.freedesktop.Telepathy.Channel.TargetID'
             ],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    requestotron = dbus.Interface(conn,
            'org.freedesktop.Telepathy.Connection.Interface.Requests')
    call_async(q, requestotron, 'CreateChannel',
            { 'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Text',
              'org.freedesktop.Telepathy.Channel.TargetHandleType': 1,
              'org.freedesktop.Telepathy.Channel.TargetHandle': foo_handle,
              })

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 2
    emitted_props = ret.value[1]
    assert emitted_props['org.freedesktop.Telepathy.Channel.ChannelType'] ==\
            'org.freedesktop.Telepathy.Channel.Type.Text'
    assert emitted_props['org.freedesktop.Telepathy.Channel.'
            'TargetHandleType'] == 1
    assert emitted_props['org.freedesktop.Telepathy.Channel.TargetHandle'] ==\
            foo_handle

    assert old_sig.args[0] == ret.value[0]
    assert old_sig.args[1] == u'org.freedesktop.Telepathy.Channel.Type.Text'
    # check that handle type == contact handle
    assert old_sig.args[2] == 1
    assert old_sig.args[3] == foo_handle
    assert old_sig.args[4] == True      # suppress handler

    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1        # one channel
    assert len(new_sig.args[0][0]) == 2     # two struct members
    assert new_sig.args[0][0][0] == ret.value[0]
    assert new_sig.args[0][0][1] == ret.value[1]

    properties = conn.GetAll(
            'org.freedesktop.Telepathy.Connection.Interface.Requests',
            dbus_interface='org.freedesktop.DBus.Properties')

    assert new_sig.args[0][0] in properties['Channels'], \
            (new_sig.args[0][0], properties['Channels'])

    call_async(q, conn, 'Disconnect')
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])


if __name__ == "__main__":
    exec_test(test)
