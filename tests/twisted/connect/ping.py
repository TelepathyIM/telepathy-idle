"""
Test Idle sending PINGs and timing out if it doesn't get a reply.
"""

from idletest import exec_test
from servicetest import assertLength, EventPattern
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    e = q.expect('stream-PING')
    assertLength(1, e.data)
    timestamp = e.data[0]
    stream.sendMessage('PONG', 'idle.test.server', ':%s' % timestamp,
        prefix='idle.test.server')

    # Apparently bip replies like this:
    e = q.expect('stream-PING')
    assertLength(1, e.data)
    timestamp = e.data[0]
    stream.sendMessage('PONG', timestamp, prefix='idle.test.server')

    q.expect('stream-PING')
    # If we don't answer Idle's ping, after some period of time Idle should
    # give up and close the connection.
    q.expect_many(
        EventPattern('irc-disconnected'),
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_NETWORK_ERROR]),
        )

if __name__ == '__main__':
    # We expect Idle to blow up the connection after three intervals without a
    # reply. So the default 5-second test timeout *should* just be enough, but
    # let's not risk it.
    exec_test(test, timeout=10, params={
        'keepalive-interval': 1,
    })

