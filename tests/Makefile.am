check_PROGRAMS = \
	test-ctcp-tokenize \
	test-ctcp-kill-blingbling \
	test-text-encode-and-split

test_ctcp_tokenize_LDADD = \
	$(top_builddir)/src/libidle-convenience.la \
	$(ALL_LIBS)

test_ctcp_kill_blingbling_LDADD = \
	$(top_builddir)/src/libidle-convenience.la \
	$(ALL_LIBS)

test_text_encode_and_split_LDADD = \
	$(top_builddir)/src/libidle-convenience.la \
	$(ALL_LIBS)

AM_CFLAGS = \
	$(ERROR_CFLAGS) \
	-I $(top_srcdir)/src \
	-I $(top_builddir)/src \
	@DBUS_CFLAGS@ \
	@GLIB_CFLAGS@ \
	@TELEPATHY_CFLAGS@

AM_LDFLAGS = \
	$(ERROR_LDFLAGS) \
	$(NULL)

ALL_LIBS = \
	@DBUS_LIBS@ \
	@GLIB_LIBS@ \
	@TELEPATHY_LIBS@

TESTS = $(check_PROGRAMS)

SUBDIRS = twisted

EXTRA_DIST= \
	meson.build \
	$(NULL)
