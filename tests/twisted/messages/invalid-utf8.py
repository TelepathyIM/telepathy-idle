# coding=utf-8
"""
Test that incoming messages containing invalid UTF-8
don't make Idle fall off the bus. This is a regression test for
bugs similar to <https://bugs.freedesktop.org/show_bug.cgi?id=30741>.
"""

from idletest import exec_test
from servicetest import assertEquals
import re

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    test_with_message(q, stream, ["I'm no ", " Buddhist"])
    test_with_message(q, stream, [u"björk"] * 3)

    test_with_message(q, stream, ["", "lolllllll"])
    test_with_message(q, stream, ["hello", ""])
    test_with_message(q, stream, "I am a stabbing robot".split(" "))

# This is the UTF-8 encoding of U+D800, which is not valid
# (not even as a noncharacter). We previously did this test with
# noncharacters, but Unicode Corrigendum #9 explicitly allows noncharacters
# to be interchanged, GLib 2.36 allows them when validating UTF-8,
# and D-Bus 1.6.10 will do likewise.
WELL_FORMED_BUT_INVALID_UTF8_BYTES = "\xed\xa0\x80"

def test_with_message(q, stream, parts):
    invalid_utf8 = WELL_FORMED_BUT_INVALID_UTF8_BYTES.join(
        part.encode('utf-8') for part in parts)

    # Idle's default character set is UTF-8. We send it a message which is
    # basically UTF-8, except that one of its code points is invalid.
    stream.sendMessage('PRIVMSG', bytes(stream.nick), ':%s' % invalid_utf8,
        prefix='remoteuser')

    # Idle should signal that *something* was received. If it hasn't validated
    # & sanitized the message properly, the dbus-daemon will kick it off.
    signal = q.expect('dbus-signal', signal='MessageReceived')

    message_parts = signal.args[0]
    text_plain = message_parts[1]
    content = text_plain['content']

    # Don't make any assumption about how many U+FFFD REPLACEMENT CHARACTERs
    # are used to replace surprising bytes.
    received_parts = [ part for part in re.split(u"\ufffd|\\?", content)
                       if part != u''
                     ]

    if parts[0] == u'björk':
        # The valid UTF-8 gets lost in transit, because we fall back
        # to assuming ASCII when g_convert() fails (this didn't happen
        # when we tested with noncharacters - oh well).
        assertEquals(['bj', 'rk', 'bj', 'rk', 'bj', 'rk'], received_parts)
    else:
        assertEquals(filter(lambda s: s != u'', parts), received_parts)

if __name__ == '__main__':
    exec_test(test)

