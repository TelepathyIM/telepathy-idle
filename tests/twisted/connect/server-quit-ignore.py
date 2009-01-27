"""
Test what happens when we send a quit command in the middle of the 'login'
sequence (e.g. between the USER command and the welcom response) to simulate the
behavior of the gimpnet server (see
http://bugs.freedesktop.org/show_bug.cgi?id=19762)
"""

from idletest import exec_test, BaseIRCServer, make_irc_event
from servicetest import EventPattern, call_async
from twisted.internet import reactor, ssl

class IgnoreQuitServer(BaseIRCServer):
    def handleUSER(self, args, prefix):
        #do nothing: don't send a welcome message
        self.event_func(make_irc_event('irc-user', None))

    def handleQUIT(self, args, prefix):
        # wait a little while and then send a welcome message
        reactor.callLater(5, self.sendWelcome)

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('irc-connected'))
    q.expect('irc-user');
    # this should send a QUIT message to the server, but the test server should not
    # respond properly, and will instead wait a while and then send a WELCOME msg
    call_async(q, conn, 'Disconnect')
    # the proper behavior of the CM is to disconnect, but the existing error is
    # that it will parse the welcome message and attempt to go to CONNECTED
    # state, which will cause an assertion to fail and the CM to crash
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]),
            EventPattern('irc-disconnected'),
            EventPattern('dbus-return', method='Disconnect'))
    return True

if __name__ == '__main__':
    exec_test(test, timeout=10, protocol=IgnoreQuitServer)

