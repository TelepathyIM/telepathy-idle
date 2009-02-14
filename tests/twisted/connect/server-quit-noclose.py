"""
Test that telepathy-idle still quits properly after a certain amount of time
even if the server doesn't close the connection after responding to the quit
request.
"""

from idletest import exec_test, BaseIRCServer, make_irc_event
from servicetest import EventPattern, call_async
from twisted.internet import reactor, ssl

class QuitNoCloseServer(BaseIRCServer):
    def handleQUIT(self, args, prefix):
        quit_msg = ' '.join(args).rstrip('\r\n')
        self.sendMessage('ERROR', ':Closing Link: idle.test.server (Quit: %s)' % quit_msg)
        # don't call self.transport.loseConnection()

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    call_async(q, conn, 'Disconnect')
    # the test server won't drop the connection upon receiving a QUIT message --
    # test that we still exit and respond properly after a certain amount of time
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]),
            EventPattern('irc-disconnected'),
            EventPattern('dbus-return', method='Disconnect'))
    return True

if __name__ == '__main__':
    exec_test(test, timeout=10, protocol=QuitNoCloseServer)

