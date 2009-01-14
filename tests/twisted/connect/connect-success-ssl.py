
"""
Test connecting to a SSL server.
"""

import dbus
from idletest import exec_test, SSLIRCServer

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_racy('dbus-signal', signal='StatusChanged', args=[1, 1])
    q.expect_racy('irc-connected')
    q.expect('dbus-signal', signal='SelfHandleChanged',
        args=[1L])
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])
    return True

if __name__ == '__main__':
    exec_test(test, {'use-ssl':dbus.Boolean(True)}, protocol=SSLIRCServer, timeout=10)

