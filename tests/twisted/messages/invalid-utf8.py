# coding=utf-8
"""
Test that incoming messages containing well-formed but invalid UTF-8 code
points don't make Idle fall off the bus. This is a regression test for
<https://bugs.freedesktop.org/show_bug.cgi?id=30741>.
"""

from idletest import exec_test
from servicetest import assertEquals

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    test_with_message(q, stream, ["I'm no ", " Buddhist"])
    # Check that valid exotic characters don't get lost
    test_with_message(q, stream, [u"björk"] * 5)

    test_with_message(q, stream, ["", "lolllllll"])
    test_with_message(q, stream, ["hello", ""])
    test_with_message(q, stream, "I am a stabbing robot".split(" "))

# This is the UTF-8 encoding of U+FDD2, which is not a valid Unicode character.
WELL_FORMED_BUT_INVALID_UTF8_BYTES = "\xef\xb7\x92"

def test_with_message(q, stream, parts):
    invalid_utf8 = WELL_FORMED_BUT_INVALID_UTF8_BYTES.join(
        part.encode('utf-8') for part in parts)

    # Idle's default character set is UTF-8. We send it a message which is
    # basically UTF-8, except that one of its code points is invalid.
    stream.sendMessage('PRIVMSG', stream.nick, ':%s' % invalid_utf8,
        prefix='remoteuser')

    # Idle should signal that *something* was received. If it hasn't validated
    # & sanitized the message properly, the dbus-daemon will kick it off.
    signal = q.expect('dbus-signal', signal='MessageReceived')

    message_parts = signal.args[0]
    text_plain = message_parts[1]
    content = text_plain['content']

    # Don't make any assumption about how many U+FFFD REPLACEMENT CHARACTERs
    # are used to replace surprising bytes.
    received_parts = [ part for part in content.split(u"\ufffd")
                       if part != u''
                     ]
    assertEquals(filter(lambda s: s != u'', parts), received_parts)

if __name__ == '__main__':
    exec_test(test)

