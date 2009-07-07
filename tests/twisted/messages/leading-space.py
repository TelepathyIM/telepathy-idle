
"""
Test that messages that are sent with a leading space are parsed correctly by
telepathy-idle.  This is a regression test for a long-standing bug that was
recently fixed
"""

from idletest import exec_test, BaseIRCServer
from servicetest import EventPattern, call_async
from constants import *
import dbus

MESSAGE_WITH_LEADING_SPACE=' This is a message with a leading space'

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    # send a message with a leading space
    stream.sendMessage('PRIVMSG', stream.nick,
            ':%s' % MESSAGE_WITH_LEADING_SPACE, prefix='remoteuser')
    # If telepathy-idle parses this message correctly, it should emit a
    # 'Received' signal
    q.expect('dbus-signal', signal='Received',
            predicate=lambda x: x.args[5]==MESSAGE_WITH_LEADING_SPACE)

    call_async(q, conn, 'Disconnect')
    return True

if __name__ == '__main__':
    exec_test(test)

