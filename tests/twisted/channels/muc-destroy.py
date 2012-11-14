"""
Tests Destroy()ing a MUC.
"""

from servicetest import call_async, wrap_channel, EventPattern
from idletest import exec_test
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    call_async(q, conn.Requests, "CreateChannel", {
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.ROOM_NAME: "#everythingyoutouch",
    })
    q.expect('stream-JOIN')
    event = q.expect('dbus-return', method='CreateChannel')
    path, props = event.value
    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text',
        ['Destroyable'])

    call_async(q, chan.Destroyable, "Destroy")
    q.expect_many(
        EventPattern('stream-PART'),
        EventPattern('dbus-signal', signal='Closed', path=path),
        EventPattern('dbus-return', method='Destroy'),
    )

if __name__ == '__main__':
    exec_test(test)
