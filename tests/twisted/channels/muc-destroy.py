"""
Tests Destroy()ing a MUC.
"""

from servicetest import call_async, wrap_channel, EventPattern, assertLength
from idletest import exec_test
import constants as cs

CHANNEL = "#everythingyoutouch"

def join(q, bus, conn):
    call_async(q, conn.Requests, "CreateChannel", {
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.ROOM_NAME: CHANNEL,
    })
    q.expect('stream-JOIN')
    event = q.expect('dbus-return', method='CreateChannel')
    path, props = event.value
    return wrap_channel(bus.get_object(conn.bus_name, path), 'Text',
        ['Destroyable', 'Messages'])

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    chan = join(q, bus, conn)

    stream.sendMessage('PRIVMSG', CHANNEL, ":who's underlined you?", prefix='marnie')
    q.expect('dbus-signal', signal='MessageReceived')

    # Without acking the message, destroy the channel.
    call_async(q, chan.Destroyable, "Destroy")
    q.expect_many(
        EventPattern('stream-PART'),
        EventPattern('dbus-signal', signal='Closed', path=chan.object_path),
        EventPattern('dbus-signal', signal='ChannelClosed', args=[chan.object_path]),
        EventPattern('dbus-return', method='Destroy'),
    )

    # Now Create it again. If we haven't actually left the channel, this will
    # fail.
    chan = join(q, bus, conn)

    # The message should be gone.
    messages = chan.Properties.Get(cs.CHANNEL_TYPE_TEXT, 'PendingMessages')
    assertLength(0, messages)


if __name__ == '__main__':
    exec_test(test)
