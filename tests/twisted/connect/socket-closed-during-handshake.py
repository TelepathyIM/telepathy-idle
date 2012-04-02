"""
Test connecting to a server which closes the socket mid-login handshake.

https://bugs.freedesktop.org/show_bug.cgi?id=48084
"""

from idletest import exec_test, BaseIRCServer

class DropConnectionServer(BaseIRCServer):
    def handleNICK(self, args, prefix):
        self.transport.loseConnection()

def test(q, bus, conn, stream):
    conn.Connect()
    # Idle should start to connect...
    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])

    # ...and then give up with an error when the server drops the connection
    # mid-handshake.
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 2])

if __name__ == '__main__':
    exec_test(test, protocol=DropConnectionServer)

