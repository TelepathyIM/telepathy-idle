ewl_conf = configuration_data()
ewl_conf.set('abs_top_builddir', abs_top_builddir)
ewl_conf.set('abs_top_srcdir', abs_top_srcdir)
ewl_conf.set('omit_libtool', 'yes')

configure_file(
	input: 'exec-with-log.sh.in',
	output: '@BASENAME@',
	configuration: ewl_conf,
)

configure_file(
	input: 'tmp-session-bus.conf.in',
	output: '@BASENAME@',
	configuration: ewl_conf,
)

# D-Bus service file for testing
configure_file(
	input: 'idle.service.in',
	output: 'org.freedesktop.Telepathy.ConnectionManager.idle.service',
	configuration: ewl_conf,
)
