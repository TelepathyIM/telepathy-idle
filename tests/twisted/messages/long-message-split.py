
"""
Test that long messages (over 512 characters -- the IRC max message length)
are split properly and sent in multiple messages that are sent within the same
message delivery timeout (Bug #17392)
"""

from idletest import exec_test, BaseIRCServer
from servicetest import EventPattern, call_async
import dbus

class LongMessageMangler(BaseIRCServer):
    def handlePRIVMSG(self, args, prefix):
        #chain up to the base class implementation which simply signals a privmsg event
        #BaseIRCServer.handlePRIVMSG(self, args, prefix)
        sender = prefix
        recipient = args[0]
        sent_message = args[1]
        # 'bounce' the message back to all participants, but truncate to the
        # max IRC message size
        return_msg = ':%s!idle.test.server PRIVMSG %s :%s' % (self.nick, recipient, sent_message)
        # 510 rather than 512 since sendLine will tack on \r\n
        self.sendLine(return_msg[:510])


LONG_MESSAGE='one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty twenty-one twenty-two twenty-three twenty-four twenty-five twenty-six twenty-seven twenty-eight twenty-nine thirty thirty-one thirty-two thirty-three thirty-four thirty-five thirty-six thirty-seven thirty-eight thirty-nine forty forty-one forty-two forty-three forty-four forty-five forty-six forty-seven forty-eight forty-nine fifty fifty-one fifty-two fifty-three fifty-four fifty-five fifty-six fifty-seven fifty-eight fifty-nine sixty sixty-one sixty-two sixty-three sixty-four sixty-five sixty-six sixty-seven sixty-eight sixty-nine'

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
    # send a whole bunch of messages in a row
    call_async(q, text_chan, 'Send', 0, LONG_MESSAGE)

    # apparently we only emit one 'Sent' signal even if we split a message up
    # and send it in multiple messages
    q.expect('dbus-signal', signal='Sent')

    part1 = q.expect('dbus-signal', signal='Received')
    n = len(part1.args[5])
    assert n <= 512, "Message exceeds IRC maximum: %d" % n
    part2 = q.expect('dbus-signal', signal='Received')
    n = len(part2.args[5])
    assert n <= 512, "Message exceeds IRC maximum: %d" % n
    received_msg = part1.args[5] + part2.args[5]

    assert received_msg == LONG_MESSAGE, received_msg

    call_async(q, conn, 'Disconnect')
    return True

if __name__ == '__main__':
    exec_test(test, protocol=LongMessageMangler)

