# coding: utf-8
"""
Regression test to check that we accept incoming nicks beginning with '-' (in
particular, "-bip"), which are illegal per the RFC but occur in the wild.
"""

from idletest import exec_test
from servicetest import EventPattern, call_async
import dbus

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    stream.sendMessage('PRIVMSG', stream.nick, ':testing testing', prefix='-bip')
    q.expect('dbus-signal', signal='MessageReceived')
    # FIXME: we should be lenient and accept unicode nicks that we recieve
    # from remote servers, but twisted can't seem to send unicode text so I
    # don't seem to be able to test this :(
    #stream.sendMessage('PRIVMSG', stream.nick, ':testing testing', prefix=u'김정은')
    #q.expect('dbus-signal', signal='MessageReceived')
    stream.sendMessage('PRIVMSG', stream.nick, ':testing testing', prefix='12foo')
    q.expect('dbus-signal', signal='MessageReceived')

    call_async(q, conn, 'Disconnect')
    return True

if __name__ == '__main__':
    exec_test(test)

