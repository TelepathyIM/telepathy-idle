
"""
Infrastructure code for testing Idle by pretending to be an IRC server.
"""

import os
import sys
import dbus
import servicetest
import twisted
from twisted.words.protocols import irc
from twisted.internet import reactor, ssl

def make_irc_event(type, data):
    event = servicetest.Event(type, data=data)
    return event

def make_connected_event():
    event = make_irc_event('irc-connected', None)
    return event

def make_disconnected_event():
    event = make_irc_event('irc-disconnected', None)
    return event

def make_privmsg_event(recipient, msg):
    event = make_irc_event('irc-privmsg', {'recipient':recipient, 'message':msg})
    return event

class BaseIRCServer(irc.IRC):
    def __init__(self, event_func):
        self.event_func = event_func
        self.authenticated = False
        self.user = None
        self.nick = None
        self.passwd = None
        self.require_pass = False

    def listen(self, port, factory):
        print ("BaseIRCServer listening...")
        return reactor.listenTCP(port, factory)

    def connectionMade(self):
        print ("connection Made")
        self.event_func(make_connected_event())

    def connectionLost(self, reason):
        print ("connection Lost  %s" % reason)
        self.event_func(make_disconnected_event())

    def dataReceived(self, data):
        print ("data received: %s" % (data,))
        (prefix, command, args) = irc.parsemsg(data)
        if command == 'PRIVMSG':
            self.event_func(make_privmsg_event(args[0], ' '.join(args[1:]).rstrip('\r\n')))
        #handle 'login' handshake
        elif command == 'PASS':
            self.passwd = args[0]
        elif command == 'NICK':
            self.nick = args[0]
        elif command == 'USER':
            self.user = args[0]
            if ((not self.require_pass) or (self.passwd is not None)) \
                and (self.nick is not None and self.user is not None):
                    self.sendWelcome()
        elif command == 'JOIN':
            print ("Joined channel %s" % args[0])
            self.sendMessage('JOIN', args[0], prefix=self.nick)
        elif command == 'QUIT':
            self.transport.loseConnection()

    def sendWelcome(self):
        self.sendMessage('001', self.nick, ':Welcome to the test IRC Network', prefix='idle.test.server')

class SSLIRCServer(BaseIRCServer):
    def listen(self, port, factory):
        print ("SSLIRCServer listening...")
        return reactor.listenSSL(port, factory,
                ssl.DefaultOpenSSLContextFactory("tools/idletest.key",
                    "tools/idletest.cert"))

def install_colourer():
    def red(s):
        return '\x1b[31m%s\x1b[0m' % s

    def green(s):
        return '\x1b[32m%s\x1b[0m' % s

    patterns = {
        'handled': green,
        'not handled': red,
        }

    class Colourer:
        def __init__(self, fh, patterns):
            self.fh = fh
            self.patterns = patterns

        def write(self, s):
            f = self.patterns.get(s, lambda x: x)
            self.fh.write(f(s))

    sys.stdout = Colourer(sys.stdout, patterns)
    return sys.stdout

def start_server(event_func, protocol=None, port=6900):
    # set up IRC server

    if protocol is None:
        protocol = BaseIRCServer

    server = protocol(event_func)
    factory = twisted.internet.protocol.Factory()
    factory.protocol = lambda *args: server
    port = server.listen(port, factory)
    return (server, port)

def make_connection(bus, event_func, params=None):
    default_params = {
        'account': 'test',
        'server': 'localhost',
        'password': '',
        'fullname': 'Test User',
        'charset': 'UTF-8',
        'quit-message': 'happy testing...',
        'use-ssl': dbus.Boolean(False),
        'port': dbus.UInt32(6900),
        }

    if params:
        default_params.update(params)

    return servicetest.make_connection(bus, event_func, 'idle', 'irc',
        default_params)

def exec_test_deferred (funs, params, protocol=None, timeout=None):
    colourer = None

    if sys.stdout.isatty():
        colourer = install_colourer()

    queue = servicetest.IteratingEventQueue(timeout)
    queue.verbose = (
        os.environ.get('CHECK_TWISTED_VERBOSE', '') != ''
        or '-v' in sys.argv)

    bus = dbus.SessionBus()
    # conn = make_connection(bus, queue.append, params)
    (server, port) = start_server(queue.append, protocol=protocol)

    error = None

    try:
        for f in funs:
            conn = make_connection(bus, queue.append, params)
            f(queue, bus, conn, server)
    except Exception, e:
        import traceback
        traceback.print_exc()
        error = e

    try:
        if colourer:
          sys.stdout = colourer.fh
        d = port.stopListening()
        if error is None:
            d.addBoth((lambda *args: reactor.crash()))
        else:
            # please ignore the POSIX behind the curtain
            d.addBoth((lambda *args: os._exit(1)))

        #conn.Disconnect()

        if 'IDLE_TEST_REFDBG' in os.environ:
            # we have to wait that Gabble timeouts so the process is properly
            # exited and refdbg can generates its report
            time.sleep(5.5)

    except dbus.DBusException, e:
        pass

def exec_tests(funs, params=None, protocol=None, timeout=None):
  reactor.callWhenRunning (exec_test_deferred, funs, params, protocol, timeout)
  reactor.run()

def exec_test(fun, params=None, protocol=None, timeout=None):
  exec_tests([fun], params, protocol, timeout)

