
"""
Regression test for a bug where, if you were in a IRC channel that had the same
name as your nickname (e.g. user 'foo' in room '#foo'), all private 1:1 messages
to foo would appear to also be coming through room #foo as well (bug #19766)
"""

from idletest import exec_test, BaseIRCServer
from servicetest import EventPattern, call_async, TimeoutError
import dbus


HANDLE_TYPE_CONTACT=1
HANDLE_TYPE_ROOM=2

# same nick and channel
CHANNEL = '#foo'
NICK = 'foo'
REMOTEUSER = 'remoteuser'

class CustomIRCServer(BaseIRCServer):

    def handlePRIVMSG(self, args, prefix):
        #chain up to the base class implementation which simply signals a privmsg event
        BaseIRCServer.handlePRIVMSG(self, args, prefix)
        sender = prefix
        recipient = args[0]
        if (recipient == REMOTEUSER):
            # auto-reply with a private message
            self.sendMessage('PRIVMSG', self.nick, ':PRIVATE', prefix=REMOTEUSER)
        elif (recipient == self.room):
            # auto-reply to the group
            self.sendMessage('PRIVMSG', self.room, ':GROUP', prefix=REMOTEUSER)

    def handleJOIN(self, args, prefix):
        self.room = args[0]
        self.sendMessage('JOIN', self.room, prefix=self.nick)
        self._sendNameReply(self.room, [self.nick, REMOTEUSER])

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    # join a chat room with the same name as our nick
    room_handles = conn.RequestHandles(HANDLE_TYPE_ROOM, [CHANNEL])
    call_async(q, conn, 'RequestChannel',
            'org.freedesktop.Telepathy.Channel.Type.Text', HANDLE_TYPE_ROOM,
            room_handles[0], True)
    # wait for the join to finish
    ret = q.expect('dbus-return', method='RequestChannel')
    q.expect('dbus-signal', signal='MembersChanged')

    # now request a private chat channel with the remote contact
    contact_handles = conn.RequestHandles(HANDLE_TYPE_CONTACT, [REMOTEUSER])
    chan_path = conn.RequestChannel('org.freedesktop.Telepathy.Channel.Type.Text',
            HANDLE_TYPE_CONTACT, contact_handles[0], True)
    chan = bus.get_object(conn.bus_name, chan_path)
    priv_text_chan = dbus.Interface(chan,
            u'org.freedesktop.Telepathy.Channel.Type.Text')

    # send a private chat message -- the test server is rigged to send a private
    # chat response
    call_async(q, priv_text_chan, 'Send', 0, 'foo')
    q.expect('irc-privmsg', data={'message': 'foo', 'recipient': REMOTEUSER})
    # the test server above is rigged to send a reply message with a leading
    # space in response to our PRIVMSG.  If telepathy-idle parses this message
    # correctly, we should emit a 'Received' signal
    event = q.expect('dbus-signal', signal='Received', predicate=lambda x: x.args[5]=='PRIVATE')
    if 'MucChannel' in event.path:
        raise RuntimeError('PRIVATE message received on group chat interface')
    else:
        # maybe the ImChannel 'Received' signal arrived first and a MucChannel
        # signal is still coming -- let's make sure it's not
        try:
            event = q.expect('dbus-signal', signal='Received', predicate=lambda x: x.args[5]=='PRIVATE')
            if 'MucChannel' in event.path:
                raise RuntimeError('PRIVATE message received on group chat interface')
        except TimeoutError:
            #ignore it -- we *want* this to time out
            pass

    call_async(q, conn, 'Disconnect')
    return True

if __name__ == '__main__':
    exec_test(test, {'account':NICK}, protocol=CustomIRCServer)

