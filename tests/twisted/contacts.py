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

    brillana, miriam = conn.RequestHandles(cs.HT_CONTACT,
        ["brillana", "miriam"])

    # First up, check that contact-id is always present
    attrs = conn.Contacts.GetContactAttributes([brillana], [], True)
    assertContains(brillana, attrs)
    brillana_attrs = attrs[brillana]
    assertContains(cs.CONN + "/contact-id", brillana_attrs)
    assertEquals("brillana", brillana_attrs[cs.CONN + "/contact-id"])


if __name__ == '__main__':
    exec_test(test)

