"""
Test what happens if the server just abruptly disconnects.
"""

from idletest import exec_test
from servicetest import EventPattern
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
        EventPattern('irc-connected'),
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
    )

    stream.transport.loseConnection()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_NETWORK_ERROR])

if __name__ == '__main__':
    exec_test(test)

