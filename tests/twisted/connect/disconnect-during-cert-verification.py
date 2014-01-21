"""
Test disconnecting while a certificate verification channel is open.
"""

import dbus
import constants as cs
from idletest import exec_test, SSLIRCServer
from servicetest import EventPattern, sync_dbus

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    q.expect('dbus-signal', signal='NewChannel')

    conn.Disconnect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged'),
            EventPattern('irc-disconnected'),
            )

    # Idle would now crash in an idle callback; so let's see if it's alive.
    sync_dbus(bus, q, conn)

if __name__ == '__main__':
    exec_test(test, {'use-ssl':dbus.Boolean(True)}, protocol=SSLIRCServer)

