
"""
Test Network error Handling with SSL servers
"""

import dbus
from idletest import exec_test, SSLIRCServer

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[1,1])
    q.expect('dbus-signal', signal='StatusChanged', args=[2,2])

if __name__ == '__main__':
    exec_test(test, {'port': dbus.UInt32(5600), 'use-ssl': dbus.Boolean(True)}, timeout=10)

