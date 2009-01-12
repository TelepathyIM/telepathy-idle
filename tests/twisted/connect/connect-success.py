
"""
Test connecting to a server.
"""

from idletest import exec_test

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])
#just hacked some stuff in to figure out what's going on
    q.expect('irc-connected')
    q.expect('irc-nick')
    q.expect('irc-user')
    #q.expect('dbus-signal', signal='PresenceUpdate',
        #args=[{1L: (0L, {u'available': {}})}])
    #q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    #conn.Disconnect()
    #q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])
    return True

if __name__ == '__main__':
    exec_test(test)

