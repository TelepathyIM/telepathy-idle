
"""
Test that long messages (over 512 characters -- the IRC max message length)
are split properly and sent in multiple messages that are sent within the same
message delivery timeout (Bug #17392)
"""

from idletest import exec_test, BaseIRCServer
from servicetest import EventPattern, call_async
from constants import *
import dbus

class LongMessageMangler(BaseIRCServer):
    host = "my.host.name"
    def get_relay_prefix(self):
        return '%s!%s@%s' % (self.nick, self.user, self.host)

    def handlePRIVMSG(self, args, prefix):
        sender = prefix
        recipient = args[0]
        sent_message = args[1]
        # 'bounce' the message back to all participants, but truncate to the
        # max IRC message size
        return_msg = ':%s PRIVMSG %s :%s' % (self.get_relay_prefix(), recipient, sent_message)
        # 510 rather than 512 since sendLine will tack on \r\n
        self.sendLine(return_msg[:510])

    def handleWHOIS(self, args, prefix):
        self.sendMessage('311', self.nick, self.nick, self.user, self.host, '*', ':Full Name', prefix='idle.test.server')


LONG_MESSAGE='one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty twenty-one twenty-two twenty-three twenty-four twenty-five twenty-six twenty-seven twenty-eight twenty-nine thirty thirty-one thirty-two thirty-three thirty-four thirty-five thirty-six thirty-seven thirty-eight thirty-nine forty forty-one forty-two forty-three forty-four forty-five forty-six forty-seven forty-eight forty-nine fifty fifty-one fifty-two fifty-three fifty-four fifty-five fifty-six fifty-seven fifty-eight fifty-nine sixty sixty-one sixty-two sixty-three sixty-four sixty-five sixty-six sixty-seven sixty-eight sixty-nine'

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    CHANNEL_NAME = '#idletest'
    call_async(q, conn.Requests, 'CreateChannel',
            { CHANNEL_TYPE: CHANNEL_TYPE_TEXT,
              TARGET_HANDLE_TYPE: HT_ROOM,
              TARGET_ID: CHANNEL_NAME })

    ret = q.expect('dbus-return', method='CreateChannel')
    q.expect('dbus-signal', signal='MembersChanged')
    chan = bus.get_object(conn.bus_name, ret.value[0])

    text_chan = dbus.Interface(chan, CHANNEL_TYPE_TEXT)
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

