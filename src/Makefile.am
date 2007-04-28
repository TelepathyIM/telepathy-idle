BUILT_SOURCES = \
		idle-server-connection-iface-signals-marshal.h \
		idle-server-connection-iface-signals-marshal.c

# correctly clean the generated source files
CLEANFILES = $(BUILT_SOURCES)

bin_PROGRAMS=telepathy-idle

CORE_SOURCES = \
	idle-connection.c \
	idle-connection.h \
	idle-connection-manager.c \
	idle-connection-manager.h \
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
	idle-im-factory.c \
	idle-im-factory.h \
	idle-muc-channel.c \
	idle-muc-channel.h \
	idle-muc-factory.c \
	idle-muc-factory.h \
	idle-parser.c \
	idle-parser.h \
	idle-server-connection.c \
	idle-server-connection.h \
	idle-server-connection-iface.c \
	idle-server-connection-iface.h \
	idle-ssl-server-connection.h \
	idle-ssl-server-connection.c \
	idle-text.h \
	idle-text.c \
	$(BUILT_SOURCES)

libidle_convenience_la_SOURCES = \
    $(CORE_SOURCES)

telepathy_idle_SOURCES = \
	idle.c

telepathy_idle_LDADD = libidle-convenience.la @TELEPATHY_LIBS@

noinst_LTLIBRARIES = libidle-convenience.la

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @SOFIA_CFLAGS@ @OPENSSL_CFLAGS@ @TELEPATHY_CFLAGS@ -std=c99
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @SOFIA_LIBS@ @OPENSSL_LIBS@ @TELEPATHY_LIBS@

%-signals-marshal.h: %-signals-marshal.list Makefile
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.h

%-signals-marshal.c: %-signals-marshal.list Makefile
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.c

%-marshal.h: %-marshal.list Makefile
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.h

%-marshal.c: %-marshal.list Makefile
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.c

