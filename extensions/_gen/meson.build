xsltproc_with_flags = [xsltproc, '--nonet', '--novalid']

gen_all_xml = custom_target(
	'all.xml',
	input: [
		join_paths(tools_dir, 'identity.xsl'),
		join_paths('..', 'all.xml'),
	],
	output: 'all.xml',
	command: xsltproc_with_flags + ['--xinclude', '@INPUT@'],
	capture: true,
)

gen_svc = custom_target(
	'svc',
	input: [
		join_paths(tools_dir, 'glib-ginterface-gen.py'),
		gen_all_xml,
	],
	output: [
		'svc.h',
		'svc-gtk-doc.h',
		'svc.c',
	],
	command: [
		python,
		'@INPUT0@',
		'--filename=extensions/_gen/svc',
		'--signal-marshal-prefix=_idle_ext',
		'--include=<telepathy-glib/telepathy-glib.h>',
		'--not-implemented-func=tp_dbus_g_method_return_not_implemented',
		'--allow-unstable',
		'@INPUT1@',
		'Idle_Svc_',
	],
)

gen_enums_h = custom_target(
	'enums.h',
	input: [
		join_paths(tools_dir, 'c-constants-generator.xsl'),
		gen_all_xml,
	],
	output: 'enums.h',
	command: xsltproc_with_flags + ['--stringparam', 'mixed-case-prefix', 'Idle', '@INPUT@'],
	capture: true,
)

gen_interfaces_h = custom_target(
	'interfaces.h',
	input: [
		join_paths(tools_dir, 'c-interfaces-generator.xsl'),
		gen_all_xml,
	],
	output: 'interfaces.h',
	command: xsltproc_with_flags + ['--stringparam', 'mixed-case-prefix', 'Idle', '@INPUT@'],
	capture: true,
)
