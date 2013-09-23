"""
Test the Contacts interface.
"""

from idletest import exec_test
from servicetest import assertContains, assertEquals
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    interfaces = conn.Properties.Get(cs.CONN, "Interfaces")
    assertContains(cs.CONN_IFACE_CONTACTS, interfaces)

    brillana, miriam = conn.get_contact_handles_sync(["brillana", "miriam"])

    # First up, check that contact-id is always present
    attrs = conn.Contacts.GetContactAttributes([brillana], [])
    assertContains(brillana, attrs)
    brillana_attrs = attrs[brillana]
    assertContains(cs.CONN + "/contact-id", brillana_attrs)
    assertEquals("brillana", brillana_attrs[cs.CONN + "/contact-id"])

    # Test grabbing some aliases! Neither contact is known to have any
    # particular capitalization so they should be lowercase.
    attrs = conn.Contacts.GetContactAttributes([brillana, miriam],
        [cs.CONN_IFACE_ALIASING])
    assertContains(cs.CONN_IFACE_ALIASING + "/alias", attrs[brillana])
    assertEquals("brillana", attrs[brillana][cs.CONN_IFACE_ALIASING + "/alias"])
    assertEquals("miriam", attrs[miriam][cs.CONN_IFACE_ALIASING + "/alias"])

    # Brillana sends us a message! We learn that she's basically 14 and uses
    # stupid capitalization on the internet.
    bRiL = 'bRiLlAnA'
    stream.sendMessage('PRIVMSG', stream.nick, ':hai!!!', prefix=bRiL)

    # We don't actually care about the message; the important bit is that her
    # alias changes.
    q.expect('dbus-signal', signal='AliasesChanged', args=[{brillana: bRiL}])
    attrs = conn.Contacts.GetContactAttributes([brillana],
        [cs.CONN_IFACE_ALIASING])
    assertEquals(bRiL, attrs[brillana][cs.CONN_IFACE_ALIASING + "/alias"])

if __name__ == '__main__':
    exec_test(test)

