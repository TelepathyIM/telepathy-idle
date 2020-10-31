
test_ctcp_tokenize = executable(
	'test-ctcp-tokenize',
	sources: [
		'test-ctcp-tokenize.c',
	],
	dependencies: idle_deps,
	include_directories: [configuration_inc, src_inc],
	link_with: libidle_convenience,
)
test('test_ctcp_tokenize', test_ctcp_tokenize)

test_ctcp_kill_blingbling = executable(
	'test-ctcp-kill-blingbling',
	sources: [
		'test-ctcp-kill-blingbling.c',
	],
	dependencies: idle_deps,
	include_directories: [configuration_inc, src_inc],
	link_with: libidle_convenience,
)
test('test_ctcp_kill_blingbling', test_ctcp_kill_blingbling)

test_text_encode_and_split = executable(
	'test-text-encode-and-split',
	sources: [
		'test-text-encode-and-split.c',
	],
	dependencies: idle_deps,
	include_directories: [configuration_inc, src_inc],
	link_with: libidle_convenience,
)
test('test_text_encode_and_split', test_text_encode_and_split)

if get_option('twisted_tests')
	subdir('twisted')
endif
