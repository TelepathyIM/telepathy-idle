
"""
Infrastructure code for testing Idle by pretending to be an IRC server.
"""

import os
import sys
import dbus
import servicetest
from servicetest import (unwrap, Event)
import twisted
from twisted.words.protocols import irc
from twisted.internet import reactor, ssl

def make_irc_event(type, data):
    if data:
        data[-1] = data[-1].rstrip('\r\n')
    event = servicetest.Event(type, data=data)
    return event

def make_connected_event():
    event = make_irc_event('irc-connected', None)
    return event

def make_disconnected_event():
    event = make_irc_event('irc-disconnected', None)
    return event

class BaseIRCServer(irc.IRC):
    verbose = (os.environ.get('CHECK_TWISTED_VERBOSE', '') != '' or '-v' in sys.argv)

    def log(self, message):
        if (self.verbose):
            print(message)

    def __init__(self, event_func):
        self.event_func = event_func
        self.authenticated = False
        self.busy = True
        self.user = None
        self.nick = None
        self.passwd = None
        self.real_name = None
        self.require_pass = False
        self.rooms = []
        self.secure = False

    def listen(self, port, factory):
        self.log ("BaseIRCServer listening...")
        return reactor.listenTCP(port, factory)

    def connectionMade(self):
        self.log ("connection Made")
        self.event_func(make_connected_event())

    def connectionLost(self, reason):
        self.log ("connection Lost  %s" % reason)
        self.event_func(make_disconnected_event())

        #handle 'login' handshake
    def handlePASS(self, args, prefix):
        self.passwd = args[0]
    def handleNICK(self, args, prefix):
        self.nick = args[0]
    def handleUSER(self, args, prefix):
        self.user = args[0]
        self.real_name = args[3]
        if ((not self.require_pass) or (self.passwd is not None)) \
            and (self.nick is not None and self.user is not None):
                self.sendWelcome()

    def handleWHOIS(self, args, prefix):
        self.busy = not self.busy

        if self.busy:
            self.sendTryAgain('WHOIS')
            return

        if self.nick != args[0]:
            self.sendMessage('402', self.nick, args[0], ':No such server', prefix='idle.test.server')
            return

        self.sendMessage('311', self.nick, self.nick, self.user, 'idle.test.client', '*', ':%s' % self.real_name, prefix='idle.test.server')
        if self.rooms:
            self.sendMessage('319', self.nick, self.nick, ':%s' % ' '.join(self.rooms), prefix='idle.test.server')
        self.sendMessage('319', self.nick, self.nick, ':', prefix='idle.test.server')
        self.sendMessage('312', self.nick, self.nick, 'idle.test.server', ':Idle Test Server', prefix='idle.test.server')
        if self.secure:
            self.sendMessage('671', self.nick, self.nick, ':is using a secure connection', prefix='idle.test.server')
        self.sendMessage('378', self.nick, self.nick, ':is connecting from localhost', prefix='idle.test.server')
        self.sendMessage('317', self.nick, self.nick, '42', ':seconds idle', prefix='idle.test.server') # fake value.
        self.sendMessage('330', self.nick, self.nick, self.nick, ':is logged in as', prefix='idle.test.server')
        self.sendMessage('318', self.nick, self.nick, ':End of /WHOIS list.', prefix='idle.test.server')

    def handleJOIN(self, args, prefix):
        room = args[0]
        self.rooms.append(room)
        self.sendJoin(room, [self.nick])

    def handlePART(self, args, prefix):
        room = args[0]
        try:
            self.rooms.remove(room)
        except ValueError:
            pass

        try:
            message = args[1]
        except IndexError:
            message = None

        self.sendPart(room, self.nick, message)

    def sendJoin(self, room, members=[]):
        members.append(self.nick)

        self.sendMessage('JOIN', room, prefix=self.nick)
        self._sendNameReply(room, members)

    def sendPart(self, room, nick, message=None):
        if message is not None:
            self.sendMessage('PART', room, message, prefix=nick)
        else:
            self.sendMessage('PART', room, prefix=nick)

    def sendTryAgain(self, command):
        self.sendMessage('263', self.nick, "WHOIS", ':Please wait a while and try again.', prefix='idle.test.server')

    def _sendNameReply(self, room, members):
        #namereply
        self.sendMessage('353', '%s = %s' % (self.nick, room), ":%s" % ' '.join(members),
                prefix='idle.test.server')
        #namereply end
        self.sendMessage('366', self.nick, room, ':End of /NAMES list', prefix='idle.test.server')

    def handleQUIT(self, args, prefix):
        quit_msg = ' '.join(args)
        self.sendMessage('ERROR', ':Closing Link: idle.test.server (Quit: %s)' % quit_msg)
        self.transport.loseConnection()

    def sendWelcome(self):
        self.sendMessage('001', self.nick, ':Welcome to the test IRC Network', prefix='idle.test.server')

    def handleCommand(self, command, prefix, params):
        self.event_func(make_irc_event('stream-%s' % command, params))
        try:
            f = getattr(self, 'handle%s' % command)
            try:
                f(params, prefix)
            except Exception, e:
                self.log('handler failed: %s' % e)
        except Exception, e:
            self.log('No handler for command %s: %s' % (command, e))

class SSLIRCServer(BaseIRCServer):
    def __init__(self, event_func):
        BaseIRCServer.__init__(self, event_func)
        self.secure = True

    def listen(self, port, factory):
        self.log ("SSLIRCServer listening...")
        key_file = os.environ.get('IDLE_SSL_KEY', 'tools/idletest.key')
        cert_file = os.environ.get('IDLE_SSL_CERT', 'tools/idletest.cert')
        return reactor.listenSSL(port, factory,
                ssl.DefaultOpenSSLContextFactory(key_file, cert_file))

def sync_stream(q, stream):
    stream.sendMessage('PING', 'sup')
    q.expect('stream-PONG')

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
        'server': '127.0.0.1',
        'password': '',
        'fullname': 'Test User',
        'username': 'testuser',
        'charset': 'UTF-8',
        'quit-message': 'happy testing...',
        'use-ssl': dbus.Boolean(False),
        'port': dbus.UInt32(6900),
        'keepalive-interval': dbus.UInt32(0),
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

    bus.add_signal_receiver(
        lambda *args, **kw:
            queue.append(
                Event('dbus-signal',
                    path=unwrap(kw['path']),
                    signal=kw['member'], args=map(unwrap, args),
                    interface=kw['interface'])),
        None,       # signal name
        None,       # interface
        None,
        path_keyword='path',
        member_keyword='member',
        interface_keyword='interface',
        byte_arrays=True
        )

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

        # force Disconnect in case the test crashed and didn't disconnect
        # properly.  We need to call this async because the BaseIRCServer
        # class must do something in response to the Disconnect call and if we
        # call it synchronously, we're blocking ourself from responding to the
        # quit method.
        servicetest.call_async(queue, conn, 'Disconnect')

        if 'IDLE_TEST_REFDBG' in os.environ:
            # we have to wait for the timeout so the process is properly
            # exited and refdbg can generate its report
            time.sleep(5.5)

    except dbus.DBusException, e:
        pass

def exec_tests(funs, params=None, protocol=None, timeout=None):
  reactor.callWhenRunning (exec_test_deferred, funs, params, protocol, timeout)
  reactor.run()

def exec_test(fun, params=None, protocol=None, timeout=None):
  exec_tests([fun], params, protocol, timeout)

