
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
    e = q.expect('dbus-signal', signal='NewChannel')
    path, props = e.args

    cert = bus.get_object (conn.bus_name, props[cs.TLS_CERT_PATH])
    cert.Reject([(cs.TLS_REJECT_REASON_UNTRUSTED, cs.CERT_UNTRUSTED, {})],
        signature = 'a(usa{sv})')

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 2])
    return True

if __name__ == '__main__':
    exec_test(test, {'use-ssl':dbus.Boolean(True)}, protocol=SSLIRCServer)

