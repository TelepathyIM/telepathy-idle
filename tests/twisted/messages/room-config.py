"""
Test RoomConfig1 implementation
"""

from idletest import exec_test, sync_stream
from servicetest import EventPattern, call_async, assertContains, assertEquals, \
    wrap_channel
import constants as cs
import dbus

def setup(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # test MUC channel
    call_async(q, conn.Requests, 'CreateChannel',
        {cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
        cs.TARGET_ID: '#test'})

    ret = q.expect('dbus-return', method='CreateChannel')

    q.expect('dbus-signal', signal='MembersChanged')
    chan = wrap_channel(bus.get_object(conn.bus_name, ret.value[0]),
                        'Text', extra=['RoomConfig1'])

    sync_stream(q, stream)

    return chan

def test_props_present(q, bus, conn, stream):
    chan = setup(q, bus, conn, stream)

    props = chan.Properties.GetAll(cs.CHANNEL_IFACE_ROOM_CONFIG)
    assertContains('PasswordProtected', props)
    assertContains('Password', props)
    assertContains('Description', props)
    assertContains('Title', props)
    assertContains('ConfigurationRetrieved', props)
    assertContains('Persistent', props)
    assertContains('Private', props)
    assertContains('Limit', props)
    assertContains('Anonymous', props)
    assertContains('CanUpdateConfiguration', props)
    assertContains('PasswordHint', props)
    assertContains('Moderated', props)
    assertContains('InviteOnly', props)
    assertContains('MutableProperties', props)

    # this should do nothing
    forbidden = [EventPattern('dbus-signal', signal='PropertiesChanged')]
    q.forbid_events(forbidden)
    call_async(q, chan.RoomConfig1, 'UpdateConfiguration', {})

    sync_stream(q, stream)
    q.unforbid_events(forbidden)

def test_simple_bools(q, bus, conn, stream):
    chan = setup(q, bus, conn, stream)

    # the three easy booleans
    for (prop, mode) in [('InviteOnly', 'i'),
                         ('Moderated', 'm'),
                         ('Private', 's')]:
        # first set them all to true
        call_async(q, chan.RoomConfig1, 'UpdateConfiguration', {prop: True})
        q.expect_many(EventPattern('dbus-return', method='UpdateConfiguration'),
                      EventPattern('stream-MODE', data=['#test', '+' + mode]),
                      EventPattern('dbus-signal', signal='PropertiesChanged',
                                   args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                                         {prop: True}, []])
                      )
        # then them all to false
        call_async(q, chan.RoomConfig1, 'UpdateConfiguration', {prop: False})
        q.expect_many(EventPattern('dbus-return', method='UpdateConfiguration'),
                      EventPattern('stream-MODE', data=['#test', '-' + mode]),
                      EventPattern('dbus-signal', signal='PropertiesChanged',
                                   args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                                         {prop: False}, []])
                      )

    # set them all to true now
    call_async(q, chan.RoomConfig1, 'UpdateConfiguration',
               {'InviteOnly': True,
                'Moderated': True,
                'Private': True})

    # ... and a monster return
    q.expect_many(EventPattern('dbus-return', method='UpdateConfiguration'),
                  EventPattern('stream-MODE', data=['#test', '+i']),
                  EventPattern('stream-MODE', data=['#test', '+m']),
                  EventPattern('stream-MODE', data=['#test', '+s']),
                  EventPattern('dbus-signal', signal='PropertiesChanged',
                               args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                                     {'InviteOnly': True,
                                      'Moderated': True,
                                      'Private': True},
                                     []])
                  )

    # set only moderated to false,
    forbidden = [EventPattern('stream-MODE', data=['#test', '+i']),
                 EventPattern('stream-MODE', data=['#test', '+s'])]
    q.forbid_events(forbidden)

    call_async(q, chan.RoomConfig1, 'UpdateConfiguration',
               {'InviteOnly': True,
                'Moderated': False,
                'Private': True})

    # ... and another monster return
    q.expect_many(EventPattern('dbus-return', method='UpdateConfiguration'),
                  EventPattern('stream-MODE', data=['#test', '-m']),
                  EventPattern('dbus-signal', signal='PropertiesChanged',
                               args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                                     {'Moderated': False},
                                     []])
                  )

    sync_stream(q, stream)
    q.unforbid_events(forbidden)

def test_limit(q, bus, conn, stream):
    chan = setup(q, bus, conn, stream)

    # do nothing, really
    forbidden = [EventPattern('stream-MODE'),
                 EventPattern('dbus-signal', signal='PropertiesChanged')]
    q.forbid_events(forbidden)

    call_async(q, chan.RoomConfig1, 'UpdateConfiguration',
               {'Limit': dbus.UInt32(0)})

    q.expect_many(EventPattern('dbus-return', method='UpdateConfiguration'))

    sync_stream(q, stream)
    q.unforbid_events(forbidden)

    # set a limit
    call_async(q, chan.RoomConfig1, 'UpdateConfiguration',
               {'Limit': dbus.UInt32(1337)}) # totally 1337

    q.expect_many(EventPattern('dbus-return', method='UpdateConfiguration'),
                  EventPattern('stream-MODE', data=['#test', '+l', '1337']),
                  EventPattern('dbus-signal', signal='PropertiesChanged',
                               args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                                     {'Limit': 1337},
                                     []])
                  )

    # unset the limit
    call_async(q, chan.RoomConfig1, 'UpdateConfiguration',
               {'Limit': dbus.UInt32(0)})

    q.expect_many(EventPattern('dbus-return', method='UpdateConfiguration'),
                  EventPattern('stream-MODE', data=['#test', '-l']),
                  EventPattern('dbus-signal', signal='PropertiesChanged',
                               args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                                     {'Limit': 0},
                                     []])
                  )

def test_password(q, bus, conn, stream):
    chan = setup(q, bus, conn, stream)

    # set a password
    call_async(q, chan.RoomConfig1, 'UpdateConfiguration',
               {'Password': 'as1m0v',
                'PasswordProtected': True})

    q.expect_many(EventPattern('dbus-return', method='UpdateConfiguration'),
                  EventPattern('stream-MODE', data=['#test', '+k', 'as1m0v']),
                  EventPattern('dbus-signal', signal='PropertiesChanged',
                               args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                                     {'PasswordProtected': True,
                                      'Password': 'as1m0v'},
                                     []])
                  )

    # unset a password
    call_async(q, chan.RoomConfig1, 'UpdateConfiguration',
               {'PasswordProtected': False})

    q.expect_many(EventPattern('dbus-return', method='UpdateConfiguration'),
                  EventPattern('stream-MODE', data=['#test', '-k']),
                  EventPattern('dbus-signal', signal='PropertiesChanged',
                               args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                                     {'PasswordProtected': False,
                                      'Password': ''},
                                     []])
                  )

    # set another password
    call_async(q, chan.RoomConfig1, 'UpdateConfiguration',
               {'Password': 'balls'})

    q.expect_many(EventPattern('dbus-return', method='UpdateConfiguration'),
                  EventPattern('stream-MODE', data=['#test', '+k', 'balls']),
                  EventPattern('dbus-signal', signal='PropertiesChanged',
                               args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                                     {'PasswordProtected': True,
                                      'Password': 'balls'},
                                     []])
                  )

    # change password
    call_async(q, chan.RoomConfig1, 'UpdateConfiguration',
               {'Password': 'penguin3'})

    q.expect_many(EventPattern('dbus-return', method='UpdateConfiguration'),
                  EventPattern('stream-MODE', data=['#test', '+k', 'penguin3']),
                  EventPattern('dbus-signal', signal='PropertiesChanged',
                               args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                                     {'Password': 'penguin3'},
                                     []])
                  )

if __name__ == '__main__':
    exec_test(test_props_present)
    exec_test(test_simple_bools)
    exec_test(test_limit)
    exec_test(test_password)
