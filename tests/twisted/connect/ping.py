"""
Test Idle sending PINGs.
"""

from idletest import exec_test
from servicetest import EventPattern, call_async

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    e = q.expect('stream-PING')
    timestamp = e.data[0]
    stream.sendMessage('PONG', timestamp)

if __name__ == '__main__':
    exec_test(test, params={
        'keepalive-interval': 1,
    })

