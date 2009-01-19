
"""
Test that messages that are sent with a leading space are parsed correctly by
telepathy-idle.  This is a regression test for a long-standing bug that was
recently fixed
"""

from idletest import exec_test, BaseIRCServer
from servicetest import EventPattern, call_async
import dbus

MESSAGE_WITH_LEADING_SPACE=' This is a message with a leading space'
class LeadingSpaceIRCServer(BaseIRCServer):
    remoteuser = 'remoteuser'

    def handlePRIVMSG(self, args, prefix):
        #chain up to the base class implementation which simply signals a privmsg event
        BaseIRCServer.handlePRIVMSG(self, args, prefix)
        sender = prefix
        recipient = args[0]
        self.sendMessage('PRIVMSG', recipient, ':%s' % MESSAGE_WITH_LEADING_SPACE, prefix=self.remoteuser)

    def handleJOIN(self, args, prefix):
        room = args[0]
        self.sendMessage('JOIN', room, prefix=self.nick)
        self._sendNameReply(room, [self.nick, self.remoteuser])


def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    CHANNEL_NAME = '#idletest'
    room_handles = conn.RequestHandles(2, [CHANNEL_NAME])
    call_async(q, conn, 'RequestChannel', 'org.freedesktop.Telepathy.Channel.Type.Text', 2, room_handles[0], True)

    ret = q.expect('dbus-return', method='RequestChannel')
    q.expect('dbus-signal', signal='MembersChanged')
    chan = bus.get_object(conn.bus_name, ret.value[0])

    text_chan = dbus.Interface(chan,
        u'org.freedesktop.Telepathy.Channel.Type.Text')
    # send a message
    call_async(q, text_chan, 'Send', 0, 'foo')
    q.expect('irc-privmsg')
    # the test server above is rigged to send a reply message with a leading
    # space in response to our PRIVMSG.  If telepathy-idle parses this message
    # correctly, we should emit a 'Received' signal
    q.expect('dbus-signal', signal='Received', predicate=lambda x: x.args[5]==MESSAGE_WITH_LEADING_SPACE)

    call_async(q, conn, 'Disconnect')
    return True

if __name__ == '__main__':
    exec_test(test, timeout=10, protocol=LeadingSpaceIRCServer)

