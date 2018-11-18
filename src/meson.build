libidle_convenience = library(
	'idle-convenience',
	sources: [
		'idle-connection.c',
		'idle-connection-manager.c',
		'idle-contact-info.c',
		'idle-ctcp.c',
		'idle-debug.c',
		'idle-handles.c',
		'idle-im-channel.c',
		'idle-im-manager.c',
		'idle-muc-channel.c',
		'idle-muc-manager.c',
		'room-config.c',
		'idle-parser.c',
		'protocol.c',
		'idle-roomlist-channel.c',
		'idle-roomlist-manager.c',
		'idle-server-connection.c',
		'idle-text.c',
		'server-tls-channel.c',
		'server-tls-manager.c',
		'tls-certificate.c',
		gen_enums_h, # required by libidle_extensions
	],
	dependencies: idle_deps,
	include_directories: configuration_inc,
	link_with: libidle_extensions,
)

executable(
	'telepathy-idle',
	sources: [
		'idle.c'
	],
	dependencies: idle_deps,
	include_directories: configuration_inc,
	install: true,
	install_dir: libexecdir,
	link_with: libidle_convenience,
)

pathconf = configuration_data()
pathconf.set('libexecdir', libexecdir)

telepathy_idle_8 = configure_file(
	configuration: pathconf,
	input: 'telepathy-idle.8.in',
	output: '@BASENAME@',
)

install_man(telepathy_idle_8)
