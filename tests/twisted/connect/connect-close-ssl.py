
"""
Test connecting to a SSL server.
"""

import dbus
import constants as cs
from idletest import exec_test, SSLIRCServer
from servicetest import EventPattern, wrap_channel

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    e = q.expect('dbus-signal', signal='NewChannel')
    path, props = e.args

    channel = wrap_channel(bus.get_object(conn.bus_name, path),
        cs.CHANNEL_TYPE_SERVER_TLS_CONNECTION)
    channel.Close()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 2])

if __name__ == '__main__':
    exec_test(test, {'use-ssl':dbus.Boolean(True)}, protocol=SSLIRCServer)

