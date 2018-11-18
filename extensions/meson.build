xmls = files(
	'all.xml',
	'Connection_Interface_IRC_Command1.xml',
)

subdir('_gen')

libidle_extensions = library(
	'idle-extensions',
	sources: [
		'extensions.h',
		gen_svc[2],
		gen_enums_h,
		gen_interfaces_h,
	],
	dependencies: idle_deps,
	include_directories: include_directories('..'),
)

gen_extensions_html = custom_target(
	'extensions.html',
	input: [
		join_paths(tools_dir, 'doc-generator.xsl'),
		gen_all_xml,
	],
	output: 'extensions.html',
	command: xsltproc_with_flags + ['@INPUT@'],
	capture: true,
)
