"""
Test disconnecting before the TCP session is established.
"""

from idletest import exec_test
from servicetest import call_async

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])

    # We want the call to Disconnect to reach Idle before the call to
    # g_socket_client_connect_to_host(), which connects to this Python process,
    # completes. I tried making the Twisted infrastructure stop calling
    # .accept() but that doesn't seem to have any effect.
    #
    # But! All is not lost! Making a blocking call to Disconnect() does the
    # job, because we block in libdbus and Twisted doesn't get a chance to poll
    # the listening socket until Disconnect() returns.
    conn.Disconnect()

    # The bug was that Idle would not try to cancel the in-progress connection
    # attempt. It worked when the connection was blocked on TLS verification
    # (see disconnect-during-cert-verification.py) because closing that channel
    # (as a side-effect of disconnecting) caused the TCP connection attempt to
    # fail, but didn't work in this case.
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

