# coding: utf-8
"""
Test that we get an error when attempting to use an invalid nick
"""

import dbus
from idletest import make_connection

def connect(nick):
    bus = dbus.SessionBus()
    params = {
        'account': nick,
        'server': 'localhost',
        'password': '',
        'fullname': 'Test User',
        'charset': 'UTF-8',
        'quit-message': 'happy testing...',
        'use-ssl': dbus.Boolean(False),
        'port': dbus.UInt32(6900),
        }

    conn = make_connection(bus, None, params)
    conn.Connect(reply_handler=None, error_handler=None)
    conn.Disconnect(reply_handler=None, error_handler=None)

def test():
    try:
        connect('nick with spaces')
        raise RuntimeError('Invalid nick not rejected')
    except dbus.DBusException, e:
        assert e.get_dbus_name() == 'org.freedesktop.Telepathy.Errors.InvalidHandle'

    try:
        connect('') # empty nick
        raise RuntimeError('Invalid nick not rejected')
    except dbus.DBusException, e:
        assert e.get_dbus_name() == 'org.freedesktop.Telepathy.Errors.InvalidHandle'

    try:
        connect('#foo') # invalid chars
        raise RuntimeError('Invalid nick not rejected')
    except dbus.DBusException, e:
        assert e.get_dbus_name() == 'org.freedesktop.Telepathy.Errors.InvalidHandle'

    try:
        connect(u'김정은') # unicode
        raise RuntimeError('Invalid nick not rejected')
    except dbus.DBusException, e:
        assert e.get_dbus_name() == 'org.freedesktop.Telepathy.Errors.InvalidHandle'

    try:
        connect('-foo') # '-' not allowed as first char
        raise RuntimeError('Invalid nick not rejected')
    except dbus.DBusException, e:
        assert e.get_dbus_name() == 'org.freedesktop.Telepathy.Errors.InvalidHandle'

    # should pass succeed without an exception
    connect('good_nick')
    connect('good-nick')
    connect('{goodnick]`')

if __name__ == '__main__':
    test()
