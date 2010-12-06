
"""
Test connecting topic settings a IRC channel
"""

from idletest import exec_test
from servicetest import EventPattern, call_async, wrap_channel, assertEquals
from servicetest import sync_dbus
from constants import *
import dbus

room = "#idletest"

def get_subject_flags (channel):
  props = channel.TpProperties.ListProperties()
  for x in props:
      if x[1] == 'subject':
          return x[3]

def get_subject_id (channel):
  props = channel.TpProperties.ListProperties()
  for x in props:
      if x[1] == 'subject':
          return x[0]

def has_subject_property (event, subject_id, flags = None):
   for (k,v) in event.args[0]:
       if k == subject_id:
           if flags != None:
               assertEquals (flags, v)
           return True
   return False

def change_channel_mode (stream, mode_change):
    stream.sendMessage ('324', stream.nick, room, mode_change,
        prefix='idle.test.server')

def test_topic_write_flag (q, stream, channel, subject_id, r = 0):
    assertEquals (r | PROPERTY_FLAG_WRITE, get_subject_flags (channel))

    change_channel_mode (stream, '+t')
    q.expect('dbus-signal', signal="PropertyFlagsChanged",
      predicate= lambda e:
        has_subject_property (e, subject_id, r))
    assertEquals (r, get_subject_flags (channel))


    change_channel_mode (stream, '-t')
    q.expect('dbus-signal', signal="PropertyFlagsChanged",
      predicate= lambda e:
        has_subject_property (e, subject_id, r | PROPERTY_FLAG_WRITE))
    assertEquals (r | PROPERTY_FLAG_WRITE, get_subject_flags (channel))

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    q.expect('dbus-signal', signal='SelfHandleChanged',
        args=[1L])
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    call_async(q, conn.Requests, 'CreateChannel',
            { CHANNEL_TYPE: CHANNEL_TYPE_TEXT,
              TARGET_HANDLE_TYPE: HT_ROOM,
              TARGET_ID: room })

    q.expect('stream-JOIN')
    event = q.expect('dbus-return', method='CreateChannel')
    path = event.value[0]

    channel = wrap_channel (bus.get_object(conn.bus_name, path), 'Text')
    subject_id = get_subject_id (channel)

    # No topic set so it's not readable
    test_topic_write_flag (q, stream, channel, subject_id)

    # Someone sets the topic so it should become readable
    stream.sendMessage ('332', stream.nick, room, ':Test123',
        prefix='idle.test.server')
    q.expect('dbus-signal', signal="PropertyFlagsChanged")
    test_topic_write_flag (q, stream, channel, subject_id, PROPERTY_FLAG_READ)

    # Topic is read/write, if we get ops it should stay that way
    forbidden = [ EventPattern('dbus-signal', signal="PropertyFlagsChanged",
            predicate= lambda e: has_subject_property (e, subject_id))]
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

    sync_dbus(bus, q, conn)
    q.unforbid_events(forbidden)

    # back to normal?
    test_topic_write_flag (q, stream, channel, subject_id, PROPERTY_FLAG_READ)

    # Check if setting ops gives us write access on +t channels
    change_channel_mode (stream, '+t')
    q.expect('dbus-signal', signal="PropertyFlagsChanged",
      predicate= lambda e: has_subject_property (e, subject_id,
        PROPERTY_FLAG_READ))

    change_channel_mode (stream, '+o ' + stream.nick)
    q.expect('dbus-signal', signal="PropertyFlagsChanged",
      predicate= lambda e: has_subject_property (e, subject_id,
        PROPERTY_FLAGS_RW))

    change_channel_mode (stream, '-o ' + stream.nick)
    q.expect('dbus-signal', signal="PropertyFlagsChanged",
      predicate= lambda e: has_subject_property (e, subject_id,
        PROPERTY_FLAG_READ))

    change_channel_mode (stream, '-t')
    q.expect('dbus-signal', signal="PropertyFlagsChanged",
      predicate= lambda e: has_subject_property (e, subject_id,
        PROPERTY_FLAGS_RW))

    # And back to normal again ?
    test_topic_write_flag (q, stream, channel, subject_id, PROPERTY_FLAG_READ)
    return True

if __name__ == '__main__':
    exec_test(test)

