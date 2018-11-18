# Telepathy manager file
install_data(
	'idle.manager',
	install_dir: managerdir,
)

# Dbus service file
configure_file(
	input: 'org.freedesktop.Telepathy.ConnectionManager.idle.service.in',
	output: '@BASENAME@',
	configuration: pathconf,
	install_dir: dbus_services_dir,
)
