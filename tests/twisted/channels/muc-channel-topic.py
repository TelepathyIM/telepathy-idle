"""
Test connecting topic settings a IRC channel
"""

from idletest import exec_test, sync_stream
from servicetest import (
    EventPattern, call_async, wrap_channel, assertEquals, assertContains,
    sync_dbus
)
import dbus
from constants import *

room = "#idletest"

def expect_subject_props_changed(q, expected_changed, exact_timestamp=False):
    e = q.expect('dbus-signal', signal='PropertiesChanged',
        interface=PROPERTIES_IFACE,
        predicate=lambda e: e.args[0] == CHANNEL_IFACE_SUBJECT)

    _, changed, invalidated = e.args
    assertEquals([], invalidated)

    # If we're expecting Timestamp to be present but don't know its exact
    # value, we just check that it has some value in 'changed', and then remove
    # it from both so we can compare the remains directly.
    if not exact_timestamp:
        if 'Timestamp' in expected_changed:
            assert 'Timestamp' in changed
            del changed['Timestamp']
            del expected_changed['Timestamp']

    assertEquals(expected_changed, changed)

def expect_and_check_can_set(q, channel, can_set):
    expect_subject_props_changed(q, { 'CanSet': can_set })
    assertEquals(can_set,
        channel.Properties.Get(CHANNEL_IFACE_SUBJECT, 'CanSet'))

    if can_set:
        # FIXME: this shouldn't return until the server gets back to us with
        # RPL_TOPIC
        channel.Subject2.SetSubject('what up')
        e = q.expect('stream-TOPIC', data=[room, 'what up'])
    else:
        call_async(q, channel.Subject2, 'SetSubject', 'boo hoo')
        q.expect('dbus-error', method='SetSubject',
            name=PERMISSION_DENIED)

def change_channel_mode (stream, mode_change):
    stream.sendMessage ('324', stream.nick, room, mode_change,
        prefix='idle.test.server')

def test_can_set(q, stream, channel):
    """
    When the user's not an op, checks that flipping +t on and off again turns
    CanSet off and on again in sympathy.
    """
    assert channel.Properties.Get(CHANNEL_IFACE_SUBJECT, 'CanSet')

    change_channel_mode (stream, '+t')
    expect_and_check_can_set(q, channel, False)

    change_channel_mode (stream, '-t')
    expect_and_check_can_set(q, channel, True)

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    alice_handle, bob_handle = conn.RequestHandles(HT_CONTACT, ['alice', 'bob'])

    call_async(q, conn.Requests, 'CreateChannel',
            { CHANNEL_TYPE: CHANNEL_TYPE_TEXT,
              TARGET_HANDLE_TYPE: HT_ROOM,
              TARGET_ID: room })

    q.expect('stream-JOIN')
    event = q.expect('dbus-return', method='CreateChannel')
    path = event.value[0]

    channel = wrap_channel(bus.get_object(conn.bus_name, path), 'Text',
        ['Subject2'])

    assertContains(CHANNEL_IFACE_SUBJECT,
        channel.Properties.Get(CHANNEL, 'Interfaces'))

    # No topic set
    subject_props = channel.Properties.GetAll(CHANNEL_IFACE_SUBJECT)
    assertEquals('', subject_props['Subject'])
    assertEquals(0, subject_props['Timestamp'])
    assertEquals('', subject_props['Actor'])
    assertEquals(0, subject_props['ActorHandle'])

    # Before the topic arrives from the server, check that our API works okay.
    # FIXME: when we make SetSubject return asynchronously, this will need
    # revising.
    test_can_set(q, stream, channel)

    # We're told the channel's topic, and (in a separte message) who set it and
    # when.
    stream.sendMessage('332', stream.nick, room, ':Test123',
        prefix='idle.test.server')
    stream.sendMessage('333', stream.nick, room, 'bob', '1307802600',
        prefix='idle.test.server')

    # FIXME: signal these together, if possible.
    expect_subject_props_changed(q, { 'Subject': 'Test123' })
    expect_subject_props_changed(q,
        { 'Timestamp': 1307802600,
          'Actor': 'bob',
          'ActorHandle': bob_handle,
        }, exact_timestamp=True)

    # Another user changes the topic.
    stream.sendMessage('TOPIC', room, ':I am as high as a kite',
        prefix='alice')
    expect_subject_props_changed(q,
        { 'Subject': 'I am as high as a kite',
          'Actor': 'alice',
          'ActorHandle': alice_handle,
          'Timestamp': 1234,
        })

    test_can_set(q, stream, channel)

    # Topic is read/write, if we get ops it should stay that way
    forbidden = [
        EventPattern('dbus-signal', signal='PropertiesChanged',
            predicate=lambda e: e.args[0] == CHANNEL_IFACE_SUBJECT)
    ]
    q.forbid_events(forbidden)

    # Set ops, check that t flag becomes a no-op
    change_channel_mode (stream, '+o ' + stream.nick)
    change_channel_mode (stream, '+t')
    change_channel_mode (stream, '-t')
    change_channel_mode (stream, '-o ' + stream.nick)

    # Check that other flags don't cause issues
    change_channel_mode (stream, '+n')
    change_channel_mode (stream, '+n')

    change_channel_mode (stream, '+to ' + stream.nick)
    change_channel_mode (stream, '-to ' + stream.nick)

    sync_stream(q, stream)
    sync_dbus(bus, q, conn)
    q.unforbid_events(forbidden)

    # back to normal?
    test_can_set(q, stream, channel)

    # Check if setting ops gives us write access on +t channels
    change_channel_mode (stream, '+t')
    expect_and_check_can_set(q, channel, False)

    change_channel_mode (stream, '+o ' + stream.nick)
    expect_and_check_can_set(q, channel, True)

    change_channel_mode (stream, '-o ' + stream.nick)
    expect_and_check_can_set(q, channel, False)

    change_channel_mode (stream, '-t')
    expect_and_check_can_set(q, channel, True)

    # And back to normal again ?
    test_can_set(q, stream, channel)

    channel.Subject2.SetSubject('')
    # Verify that we send an empty final parameter ("clear the topic") as
    # opposed to no final parameter ("what is the topic").
    q.expect('stream-TOPIC', data=[room, ''])

if __name__ == '__main__':
    exec_test(test)

