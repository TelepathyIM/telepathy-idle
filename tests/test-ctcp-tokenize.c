#include <idle-ctcp.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <telepathy-glib/util.h>

int main() {
	const gchar *test_str = "\001  foo \" fo bar\" bar\\001\\002\\003baz\"for every foo there is \\001 bar\\\\\001";
	gchar **tokens = idle_ctcp_decode(test_str);

	const gchar *should_be[] = {"foo", " fo bar", "bar\001\002\003baz", "for every foo there is \001 bar\\", NULL};
	for (int i = 0; i < (sizeof(should_be) / sizeof(should_be[0])); i++) {
		if (tp_strdiff(tokens[i], should_be[i]))
			fprintf(stderr, "Should be \"%s\", is \"%s\"\n", g_strescape(should_be[i], NULL), g_strescape(tokens[i], NULL));
	}

	return 0;
}

