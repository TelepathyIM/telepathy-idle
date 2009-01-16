
"""
Test connecting to a server.
"""

from idletest import exec_test
from servicetest import EventPattern, call_async

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    q.expect('dbus-signal', signal='SelfHandleChanged',
        args=[1L])
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    call_async(q, conn, 'Disconnect')
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]),
            EventPattern('irc-disconnected'),
            EventPattern('dbus-return', method='Disconnect'))
    return True

if __name__ == '__main__':
    exec_test(test, timeout=10)

