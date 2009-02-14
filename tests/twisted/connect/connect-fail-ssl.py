
"""
Test Network error Handling with SSL servers
"""

import dbus
from idletest import exec_test, SSLIRCServer

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[1,1]) # connecting, requested
    q.expect('dbus-signal', signal='StatusChanged', args=[2,2]) # disconnected, network-error

if __name__ == '__main__':
    # there is no ssl server listening at port 5600, so this should fail
    exec_test(test, {'port': dbus.UInt32(5600), 'use-ssl': dbus.Boolean(True)})

