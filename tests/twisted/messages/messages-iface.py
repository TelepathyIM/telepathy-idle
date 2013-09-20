"""
Test Messages interface implementation
"""

from idletest import exec_test
from servicetest import EventPattern, call_async, assertContains, assertEquals
import constants as cs
import dbus

def test_sending(q, bus, conn, stream, chan):
    text_chan = dbus.Interface(chan, cs.CHANNEL_TYPE_TEXT)

    message = [
        {'message-type': cs.MT_NORMAL },
        {'content-type': 'text/plain',
         'content': 'What\'s up?',}]

    call_async(q, text_chan, 'SendMessage', message, 0)

    q.expect_many(
        EventPattern('dbus-signal', signal='MessageSent'),
        EventPattern('dbus-return', method='SendMessage'))

def test_dbus_properties (chan):
    props = chan.GetAll(cs.CHANNEL_TYPE_TEXT,
        dbus_interface=cs.PROPERTIES_IFACE)

    assert props['SupportedContentTypes'] == ['text/plain']
    assert props['MessagePartSupportFlags'] == 0
    # Don't check props['DeliveryReportingSupport'] as tp-glib uses to forget
    # this property

def check_message(conn, msg, content, message_type=cs.MT_NORMAL):
    header = msg[0]
    assertEquals('alice', header['message-sender-id'])
    mtype = header.get('message-type', cs.MT_NORMAL);
    assertEquals(message_type, mtype)

    body = msg[1]
    assertEquals(content, body['content'])
    assertEquals('text/plain', body['content-type'])

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # test MUC channel
    call_async(q, conn.Requests, 'CreateChannel',
        {cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
        cs.TARGET_ID: '#test'})

    ret = q.expect('dbus-return', method='CreateChannel')

    q.expect('dbus-signal', signal='MembersChanged')
    chan = bus.get_object(conn.bus_name, ret.value[0])

    test_sending(q, bus, conn, stream, chan)

    # Receive a message on the channel
    stream.sendMessage('PRIVMSG', '#test', ":pony!", prefix='alice')
    e = q.expect('dbus-signal', signal='MessageReceived')

    check_message(conn, e.args[0], "pony!")

    test_dbus_properties(chan)

    # Receive an action message on the channel
    stream.sendMessage('PRIVMSG', '#test', ":\001ACTION has no pony :(\001",
        prefix='alice')
    e = q.expect('dbus-signal', signal='MessageReceived')

    check_message(conn, e.args[0], "has no pony :(",
        message_type = cs.MT_ACTION)

    # test private channel
    call_async(q, conn.Requests, 'CreateChannel',
        {cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_ID: 'alice'})

    ret = q.expect('dbus-return', method='CreateChannel')
    chan = bus.get_object(conn.bus_name, ret.value[0])

    test_sending(q, bus, conn, stream, chan)

    # Receive a private message from Alice
    stream.sendMessage('PRIVMSG', stream.nick, ":i want my pony!", prefix='alice')

    e = q.expect('dbus-signal', signal='MessageReceived')

    check_message(conn, e.args[0], "i want my pony!")

    # Receive an action message in private
    stream.sendMessage('PRIVMSG', stream.nick, ":\001ACTION has no pony :(\001",
        prefix='alice')

    e = q.expect('dbus-signal', signal='MessageReceived')

    check_message(conn, e.args[0], "has no pony :(",
        message_type = cs.MT_ACTION)

    test_dbus_properties(chan)

    # we're done
    call_async(q, conn, 'Disconnect')

if __name__ == '__main__':
    exec_test(test)
