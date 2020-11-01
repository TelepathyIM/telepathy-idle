"""
Test Idle's o.fd.T.Protocol implementation
"""

import dbus
from servicetest import (unwrap, tp_path_prefix, assertEquals, assertContains,
        call_async)
from idletest import exec_test
import constants as cs

def test(q, bus, conn, server):
    cm = bus.get_object(cs.CM + '.idle',
        tp_path_prefix + '/ConnectionManager/idle')
    cm_iface = dbus.Interface(cm, cs.CM)
    cm_prop_iface = dbus.Interface(cm, cs.PROPERTIES_IFACE)

    protocols = unwrap(cm_prop_iface.Get(cs.CM, 'Protocols'))
    assertEquals(set(['irc']), set(protocols.keys()))

    protocol_names = unwrap(cm_iface.ListProtocols())
    assertEquals(set(['irc']), set(protocol_names))

    cm_params = cm_iface.GetParameters('irc')
    local_props = protocols['irc']
    local_params = local_props[cs.PROTOCOL + '.Parameters']
    assertEquals(cm_params, local_params)

    proto = bus.get_object(cm.bus_name, cm.object_path + '/irc')
    proto_iface = dbus.Interface(proto, cs.PROTOCOL)
    proto_prop_iface = dbus.Interface(proto, cs.PROPERTIES_IFACE)
    proto_props = unwrap(proto_prop_iface.GetAll(cs.PROTOCOL))

    for key in ['Parameters', 'Interfaces', 'ConnectionInterfaces',
      'RequestableChannelClasses', 'VCardField', 'EnglishName', 'Icon']:
        a = local_props[cs.PROTOCOL + '.' + key]
        b = proto_props[key]
        assertEquals(a, b)

    assertEquals('x-irc', proto_props['VCardField'])
    assertEquals('IRC', proto_props['EnglishName'])
    assertEquals('im-irc', proto_props['Icon'])

    assertContains(cs.CONN_IFACE_ALIASING, proto_props['ConnectionInterfaces'])
    assertContains(cs.CONN_IFACE_REQUESTS, proto_props['ConnectionInterfaces'])

    assertEquals('robot101', unwrap(proto_iface.NormalizeContact('Robot101')))

    call_async(q, proto_iface, 'IdentifyAccount', {'account': 'Robot101'})
    q.expect('dbus-error', method='IdentifyAccount', name=cs.INVALID_ARGUMENT)

    test_params = {'account': 'Robot101', 'server': 'irc.oftc.net' }
    acc_name = unwrap(proto_iface.IdentifyAccount(test_params))
    assertEquals('robot101@irc.oftc.net', acc_name)

    # Test validating 'username'
    test_params = {
        'account': 'Robot101',
        'server': 'irc.oftc.net',
        'username': '@@@\n\n\n',
    }
    call_async(q, proto_iface, 'IdentifyAccount', test_params)
    q.expect('dbus-error', method='IdentifyAccount', name=cs.INVALID_ARGUMENT)

if __name__ == '__main__':
    exec_test(test)
