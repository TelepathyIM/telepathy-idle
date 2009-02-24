# coding: utf-8
"""
Regression test for a bug where, if you were in a IRC channel that had the same
name as your nickname (e.g. user 'foo' in room '#foo'), all private 1:1 messages
to foo would appear to also be coming through room #foo as well (bug #19766)
"""

from idletest import exec_test
from servicetest import EventPattern, call_async
import dbus

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    stream.sendMessage('PRIVMSG', stream.nick, ':testing testing', prefix='-bip')
    q.expect('dbus-signal', signal='Received')
    # FIXME: we should be lenient and accept unicode nicks that we recieve
    # from remote servers, but twisted can't seem to send unicode text so I
    # don't seem to be able to test this :(
    #stream.sendMessage('PRIVMSG', stream.nick, ':testing testing', prefix=u'김정은')
    #q.expect('dbus-signal', signal='Received')
    stream.sendMessage('PRIVMSG', stream.nick, ':testing testing', prefix='12foo')
    q.expect('dbus-signal', signal='Received')

    call_async(q, conn, 'Disconnect')
    return True

if __name__ == '__main__':
    exec_test(test)

