"""
Test that the 'Sent' signal is emitted properly when long messages are split
into multiple messages
"""

from idletest import exec_test
from servicetest import EventPattern, call_async
import dbus

LONG_MESSAGE='one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty twenty-one twenty-two twenty-three twenty-four twenty-five twenty-six twenty-seven twenty-eight twenty-nine thirty thirty-one thirty-two thirty-three thirty-four thirty-five thirty-six thirty-seven thirty-eight thirty-nine forty forty-one forty-two forty-three forty-four forty-five forty-six forty-seven forty-eight forty-nine fifty fifty-one fifty-two fifty-three fifty-four fifty-five fifty-six fifty-seven fifty-eight fifty-nine sixty sixty-one sixty-two sixty-three sixty-four sixty-five sixty-six sixty-seven sixty-eight sixty-nine'
MESSAGE_PART_ONE='one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty twenty-one twenty-two twenty-three twenty-four twenty-five twenty-six twenty-seven twenty-eight twenty-nine thirty thirty-one thirty-two thirty-three thirty-four thirty-five thirty-six thirty-seven thirty-eight thirty-nine forty forty-one forty-two forty-thr'
MESSAGE_PART_TWO='ee forty-four forty-five forty-six forty-seven forty-eight forty-nine fifty fifty-one fifty-two fifty-three fifty-four fifty-five fifty-six fifty-seven fifty-eight fifty-nine sixty sixty-one sixty-two sixty-three sixty-four sixty-five sixty-six sixty-seven sixty-eight sixty-nine'

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

    # should emit two Sent signals since the message is long enough to be
    # split into two messages
    e = q.expect('dbus-signal', signal='Sent')
    assert e.args[2] == MESSAGE_PART_ONE, e.args[2]

    e = q.expect('dbus-signal', signal='Sent')
    assert e.args[2] == MESSAGE_PART_TWO, e.args[2]

    call_async(q, conn, 'Disconnect')
    return True

if __name__ == '__main__':
    exec_test(test)

