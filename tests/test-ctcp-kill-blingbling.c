#include <idle-ctcp.h>

#include <stdio.h>
#include <string.h>

int
main (void)
{
	gboolean fail = FALSE;

	const gchar *test_strings[] = {
		"foobar", "foobar",
		"foo \x03\x31\x33<3", "foo <3",
		NULL, NULL
	};

	for (int i = 0; test_strings[i] != NULL; i += 2) {
		gchar *killed = idle_ctcp_kill_blingbling(test_strings[i]);

		if (strcmp(killed, test_strings[i + 1])) {
			fprintf(stderr, "\"%s\" -> \"%s\", should be \"%s\"", test_strings[i], killed, test_strings[i + 1]);
			fail = TRUE;
		}

		g_free(killed);
	}

	if (fail)
		return 1;
	else
		return 0;
}

