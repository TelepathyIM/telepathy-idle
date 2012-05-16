"""
Test RoomConfig1 implementation
"""

from idletest import exec_test, sync_stream
from servicetest import EventPattern, call_async, assertContains, assertEquals, \
    wrap_channel
import constants as cs
import dbus

def change_channel_mode(stream, mode_change):
    stream.sendMessage('324', stream.nick, '#test', mode_change,
                       prefix='idle.test.server')

def setup(q, bus, conn, stream, op_user=True):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # test MUC channel
    call_async(q, conn.Requests, 'CreateChannel',
        {cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
        cs.TARGET_ID: '#test'})

    ret, _, _ = q.expect_many(EventPattern('dbus-return', method='CreateChannel'),
                              EventPattern('dbus-signal', signal='MembersChanged'),
                              EventPattern('stream-MODE', data=['#test']))

    chan = wrap_channel(bus.get_object(conn.bus_name, ret.value[0]),
                        'Text', extra=['RoomConfig1'])

    change_channel_mode(stream, '+n')

    q.expect('dbus-signal', signal='PropertiesChanged',
             args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                   {'ConfigurationRetrieved': True}, []])

    if op_user:
        change_channel_mode(stream, '+o test')

        q.expect_many(EventPattern('dbus-signal', signal='GroupFlagsChanged',
                                   args=[cs.GF_MESSAGE_REMOVE | cs.GF_CAN_REMOVE, 0]),
                      EventPattern('dbus-signal', signal='PropertiesChanged',
                                   args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                                         {'CanUpdateConfiguration': True},
                                         []]))

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

    # we should have these mutable ones
    mutable_props = ['InviteOnly',
                     'Limit',
                     'Moderated',
                     'Private',
                     'PasswordProtected',
                     'Password']
    assertEquals(mutable_props, props['MutableProperties'])

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

    # get rid of it
    call_async(q, chan.RoomConfig1, 'UpdateConfiguration',
               {'PasswordProtected': False})
    q.expect('stream-MODE', data=['#test', '-k']),

    # try some wacky values
    call_async(q, chan.RoomConfig1, 'UpdateConfiguration',
               {'PasswordProtected': True})
    q.expect('dbus-error', method='UpdateConfiguration',
             name=cs.INVALID_ARGUMENT)

    call_async(q, chan.RoomConfig1, 'UpdateConfiguration',
               {'PasswordProtected': True,
                'Password': ''})
    q.expect('dbus-error', method='UpdateConfiguration',
             name=cs.INVALID_ARGUMENT)

    call_async(q, chan.RoomConfig1, 'UpdateConfiguration',
               {'PasswordProtected': False,
                'Password': 'scumbagsteve'})
    q.expect('dbus-error', method='UpdateConfiguration',
             name=cs.INVALID_ARGUMENT)

def test_modechanges(q, bus, conn, stream):
    chan = setup(q, bus, conn, stream)

    # password
    change_channel_mode(stream, '+k bettercallsaul')
    q.expect('dbus-signal', signal='PropertiesChanged',
             args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                   {'PasswordProtected': True,
                    'Password': 'bettercallsaul'},
                   []])

    change_channel_mode(stream, '-k')
    q.expect('dbus-signal', signal='PropertiesChanged',
             args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                   {'PasswordProtected': False,
                    'Password': ''},
                   []])

    # limit
    change_channel_mode(stream, '+l 42')
    q.expect('dbus-signal', signal='PropertiesChanged',
             args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                   {'Limit': 42},
                   []])

    change_channel_mode(stream, '-l')
    q.expect('dbus-signal', signal='PropertiesChanged',
             args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                   {'Limit': 0},
                   []])

    # the other three
    for mode, prop in [('i', 'InviteOnly'),
                       ('m', 'Moderated'),
                       ('s', 'Private')]:

        change_channel_mode(stream, '+%s' % mode)
        q.expect('dbus-signal', signal='PropertiesChanged',
                 args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                       {prop: True},
                       []])

        change_channel_mode(stream, '-%s' % mode)
        q.expect('dbus-signal', signal='PropertiesChanged',
                 args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                       {prop: False},
                       []])

    # a lot in one go
    change_channel_mode(stream, '+imsk holly')
    q.expect('dbus-signal', signal='PropertiesChanged',
             args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                   {'InviteOnly': True,
                    'Moderated': True,
                    'Private': True,
                    'PasswordProtected': True,
                    'Password': 'holly'},
                   []])

def test_mode_no_op(q, bus, conn, stream):
    chan = setup(q, bus, conn, stream, op_user=False)

    # we haven't been opped, so we can't be allowed to change these
    # values
    for key, val in [('InviteOnly', True),
                     ('Moderated', True),
                     ('Private', True),
                     ('Password', True),
                     ('Limit', 99)]:
        call_async(q, chan.RoomConfig1, 'UpdateConfiguration',
                   {key: val})

        q.expect('dbus-error', method='UpdateConfiguration',
                 name=cs.PERMISSION_DENIED)

    # op the user
    change_channel_mode(stream, '+o test')
    q.expect('dbus-signal', signal='PropertiesChanged',
             args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                   {'CanUpdateConfiguration': True},
                   []])

    # remove ops again
    change_channel_mode(stream, '-o test')
    q.expect('dbus-signal', signal='PropertiesChanged',
             args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                   {'CanUpdateConfiguration': False},
                   []])

if __name__ == '__main__':
    exec_test(test_props_present)
    exec_test(test_simple_bools)
    exec_test(test_limit)
    exec_test(test_password)
    exec_test(test_modechanges)
    exec_test(test_mode_no_op)
