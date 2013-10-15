"""
Test Messages interface implementation
"""

from idletest import exec_test
from servicetest import call_async
import constants as cs
import dbus

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    irc_cmd = dbus.Interface(conn, cs.CONN + '.Interface.IRCCommand1')

    call_async(q, irc_cmd, 'Send', 'badger mushroom snake')

    q.expect('stream-BADGER', data=['mushroom', 'snake'])

    q.expect('dbus-return', method='Send')

    # We are not supposed to use this API to send messages
    call_async(q, irc_cmd, 'Send', 'PRIVMSG badger :oh hi')

    q.expect('dbus-error', method='Send', name=cs.INVALID_ARGUMENT)

    call_async(q, conn, 'Disconnect')

if __name__ == '__main__':
    exec_test(test)
