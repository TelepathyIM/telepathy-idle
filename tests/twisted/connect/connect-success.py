
"""
Test connecting to a server.
"""

from idletest import exec_test

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
    exec_test(test, timeout=10)

