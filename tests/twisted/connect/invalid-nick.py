# coding: utf-8
"""
Test that we get an error when attempting to use an invalid nick
"""

import dbus

def make_connection(nick):
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

    cm = bus.get_object(
        'org.freedesktop.Telepathy.ConnectionManager.idle',
        '/org/freedesktop/Telepathy/ConnectionManager/idle')
    cm_iface = dbus.Interface(cm, 'org.freedesktop.Telepathy.ConnectionManager')

    connection_name, connection_path = cm_iface.RequestConnection(
        'irc', params)

def test():
    try:
        make_connection('nick with spaces')
        raise RuntimeError('Invalid nick not rejected')
    except dbus.DBusException, e:
        pass    # nick rejected properly with an error

    try:
        make_connection('') # empty nick
        raise RuntimeError('Invalid nick not rejected')
    except dbus.DBusException, e:
        pass    # nick rejected properly with an error

    try:
        make_connection('#foo') # invalid chars
        raise RuntimeError('Invalid nick not rejected')
    except dbus.DBusException, e:
        pass    # nick rejected properly with an error

    try:
        make_connection(u'김정은') # unicode
        raise RuntimeError('Invalid nick not rejected')
    except dbus.DBusException, e:
        pass    # nick rejected properly with an error

    # should pass succeed without an exception
    make_connection('good_nick')

if __name__ == '__main__':
    test()
