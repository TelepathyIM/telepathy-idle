twisted_tests = [
	'cm/protocol.py',
	'connect/connect-close-ssl.py',
	'connect/connect-success.py',
	'connect/connect-success-ssl.py',
	'connect/connect-reject-ssl.py',
	'connect/connect-fail.py',
	'connect/connect-fail-ssl.py',
	'connect/disconnect-before-socket-connected.py',
	'connect/disconnect-during-cert-verification.py',
	'connect/ping.py',
	'connect/server-quit-ignore.py',
	'connect/server-quit-noclose.py',
	'connect/socket-closed-after-handshake.py',
	'connect/socket-closed-during-handshake.py',
	'connect/invalid-nick.py',
	'contacts.py',
	'channels/join-muc-channel.py',
	'channels/join-muc-channel-bouncer.py',
	'channels/requests-create.py',
	'channels/requests-muc.py',
	'channels/muc-channel-topic.py',
	'channels/muc-destroy.py',
	'channels/room-list-channel.py',
	'channels/room-list-multiple.py',
	'irc-command.py',
	'messages/accept-invalid-nicks.py',
	'messages/contactinfo-request.py',
	'messages/invalid-utf8.py',
	'messages/messages-iface.py',
	'messages/message-order.py',
	'messages/leading-space.py',
	'messages/long-message-split.py',
	'messages/room-contact-mixup.py',
	'messages/room-config.py',
]

twisted_conf = configuration_data()
twisted_conf.set_quoted('PACKAGE_STRING', package_string)

configure_file(
	input: 'config.py.in',
	output: '@BASENAME@',
	configuration: twisted_conf,
)

if get_option('twisted_sleep') == '0'
	idle_test_sleep = ''
else
	idle_test_sleep = '--sleep=@0@'.format(get_option('twisted_sleep'))
endif

# idle-twisted-tests.list
test_list = custom_target('idle-twisted-tests.list',
    output: 'idle-twisted-tests.list',
    command: ['echo', '\n'.join(twisted_tests)],
    capture: true,
)

# We don't really use idletestsdir yet - we only support uninstalled testing
# so far - but I'm substituting it to keep the script more similar to Gabble's.
# ${pkglibexecdir}/tests is what GNOME's InstalledTests goal recommends.

abs_top_srcdir = meson.source_root()
abs_top_builddir = meson.build_root()

run_test_conf = configuration_data()
run_test_conf.set('idletestsdir', join_paths(pkglibexecdir, 'tests'))
run_test_conf.set('TEST_PYTHON', test_python)

run_test = configure_file(
	input: 'run-test.sh.in',
	output: '@BASENAME@',
	configuration: run_test_conf,
)

test(
	'check',
	run_test,
	args: twisted_tests,
	env: [
		'IDLE_TEST_UNINSTALLED=1',
		'IDLE_ABS_TOP_SRCDIR=@0@'.format(abs_top_srcdir),
		'IDLE_ABS_TOP_BUILDDIR=@0@'.format(abs_top_builddir),
		'IDLE_TEST_SLEEP=@0@'.format(idle_test_sleep),
	],
	depends: [
		test_list,
	],
	suite: 'twisted',
)

subdir('tools')
