
"""
Test connecting to a SSL server.
"""

import dbus
import constants as cs
from idletest import exec_test, SSLIRCServer
from servicetest import EventPattern, call_async

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    e = q.expect('dbus-signal', signal='NewChannels')
    channels = e.args[0]
    path, props = channels[0]

    cert = bus.get_object (conn.bus_name, props[cs.TLS_CERT_PATH])
    cert.Accept()

    q.expect('dbus-signal', signal='SelfHandleChanged',
        args=[1])
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    call_async(q, conn, 'Disconnect')
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]),
            EventPattern('irc-disconnected'),
            EventPattern('dbus-return', method='Disconnect'))
    return True

if __name__ == '__main__':
    exec_test(test, {'use-ssl':dbus.Boolean(True)}, protocol=SSLIRCServer)

