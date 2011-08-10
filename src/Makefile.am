BUILT_SOURCES = \
		idle-server-connection-signals-marshal.h \
		idle-server-connection-signals-marshal.c

# correctly clean the generated source files
CLEANFILES = $(BUILT_SOURCES) $(man_MANS)

libexec_PROGRAMS=telepathy-idle

libidle_convenience_la_SOURCES = \
	idle-connection.c \
	idle-connection.h \
	idle-connection-manager.c \
	idle-connection-manager.h \
	idle-contact-info.c \
	idle-contact-info.h \
	idle-ctcp.c \
	idle-ctcp.h \
	idle-debug.c \
	idle-debug.h \
	idle-dns-resolver.c \
	idle-dns-resolver.h \
	idle-handles.c \
	idle-handles.h \
	idle-im-channel.c \
	idle-im-channel.h \
	idle-im-manager.c \
	idle-im-manager.h \
	idle-muc-channel.c \
	idle-muc-channel.h \
	idle-muc-manager.c \
	idle-muc-manager.h \
	idle-parser.c \
	idle-parser.h \
	protocol.c \
	protocol.h \
	idle-server-connection.c \
	idle-server-connection.h \
	idle-text.h \
	idle-text.c \
	idle-server-connection-signals-marshal.list

nodist_libidle_convenience_la_SOURCES = \
	$(BUILT_SOURCES)

libidle_convenience_la_LIBADD = \
	$(top_builddir)/extensions/libidle-extensions.la

telepathy_idle_SOURCES = \
	idle.c

telepathy_idle_LDADD = \
	libidle-convenience.la \
	$(ALL_LIBS)

noinst_LTLIBRARIES = libidle-convenience.la

AM_CFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	$(ERROR_CFLAGS) \
	@DBUS_CFLAGS@ \
	@GLIB_CFLAGS@ \
	@OPENSSL_CFLAGS@ \
	@TELEPATHY_CFLAGS@ \
	-std=c99

ALL_LIBS = \
	@DBUS_LIBS@ \
	@GLIB_LIBS@ \
	@OPENSSL_LIBS@ \
	@TELEPATHY_LIBS@

%-signals-marshal.h: %-signals-marshal.list Makefile
	$(AM_V_GEN)glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.h

%-signals-marshal.c: %-signals-marshal.list Makefile
	$(AM_V_GEN)glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.c

%-marshal.h: %-marshal.list Makefile
	$(AM_V_GEN)glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.h

%-marshal.c: %-marshal.list Makefile
	$(AM_V_GEN)glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.c

man_MANS = telepathy-idle.8

EXTRA_DIST = telepathy-idle.8.in

%.8: %.8.in Makefile
	$(AM_V_GEN)sed -e 's,[@]libexecdir[@],@libexecdir@,' < $< > $@
