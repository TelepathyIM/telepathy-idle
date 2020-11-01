# Making releases:
#   set the new version number:
#     odd minor -> development series
#     even minor -> stable series
#     increment micro for each release within a series
#   set nano_version to 0
#   make the release, tag it
#   set nano_version to 1

project(
	'telepathy-idle',
	'c',
	version: '0.2.2.0',
	default_options: [
		'c_std=c99',
	],
	meson_version: '>= 0.50.0',
)

package_string = '@0@-@1@'.format(meson.project_name(), meson.project_version())

cc = meson.get_compiler('c')

glib = dependency('glib-2.0', version: '>= 2.32.0')
gobject = dependency('gobject-2.0', version: '>= 2.32.0')
gio = dependency('gio-2.0', version: '>= 2.32.0')
dbus = dependency('dbus-1', version: '>= 0.51')
dbus_glib = dependency('dbus-glib-1', version: '>= 0.51')
telepathy_glib = dependency('telepathy-glib', version: '>= 0.23.0')

idle_deps = [
	glib,
	gobject,
	gio,
	dbus,
	dbus_glib,
	telepathy_glib,
]

xsltproc = find_program('xsltproc')
python = import('python').find_installation('python3')

# check for a version of python that can run the twisted tests
if get_option('twisted_tests')
	if run_command(python, '-c', '''from sys import version_info; import dbus, dbus.mainloop.glib; raise SystemExit(version_info < (2, 5, 0, 'final', 0))''').returncode() != 0 or run_command(python, '-c', '''import twisted.words.protocols.irc, twisted.internet.reactor''').returncode() != 0
		error('Missing twisted IRC support')
	endif

	test_python = python.path()
endif

# Directories

datadir = join_paths(get_option('prefix'), get_option('datadir'))
libexecdir = join_paths(get_option('prefix'), get_option('libexecdir'))
pkglibexecdir = join_paths(libexecdir, meson.project_name())
dbus_services_dir = join_paths(datadir, 'dbus-1', 'services')
managerdir = join_paths(datadir, 'telepathy', 'managers')

# Configuration

conf_data = configuration_data()
conf_data.set_quoted('DBUS_SERVICES_DIR', dbus_services_dir, description: 'DBus services directory')
conf_data.set('GLIB_VERSION_MIN_REQUIRED', 'GLIB_VERSION_2_28', description: 'Ignore post 2.28 deprecations')
conf_data.set('GLIB_VERSION_MAX_ALLOWED', 'GLIB_VERSION_2_32', description: 'Prevent post 2.32 APIs')
conf_data.set('TP_SEAL_ENABLE', '', description: 'Prevent to use sealed variables')
conf_data.set('TP_DISABLE_SINGLE_INCLUDE', '', description: 'Disable single header include')
conf_data.set('TP_VERSION_MIN_REQUIRED', 'TP_VERSION_0_24', description: 'Ignore post 0.24 deprecations')
conf_data.set('TP_VERSION_MAX_ALLOWED', 'TP_VERSION_0_24', description: 'Prevent post 0.24 APIs')
conf_data.set_quoted('VERSION', meson.project_version())
if cc.has_function('strnlen')
	conf_data.set10('HAVE_STRNLEN', true)
endif

configure_file(
	output: 'config.h',
	configuration: conf_data,
)

configuration_inc = include_directories('.')
src_inc = include_directories('src')
tools_dir = join_paths(meson.current_source_dir(), 'tools')

subdir('extensions')
subdir('src')
subdir('data')
subdir('tests')
