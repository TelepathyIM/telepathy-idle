BUILT_SOURCES = \
		idle-connection-manager-glue.h \
		idle-connection-manager-signals-marshal.h \
		idle-connection-manager-signals-marshal.c \
		idle-connection-signals-marshal.h \
		idle-connection-signals-marshal.c \
		idle-server-connection-iface-signals-marshal.h \
		idle-server-connection-iface-signals-marshal.c \
		config.h

# correctly clean the generated source files
CLEANFILES = $(BUILT_SOURCES)

bin_PROGRAMS=telepathy-idle

CORE_SOURCES = \
	idle-connection-manager.h \
	idle-connection.h \
	idle-im-channel.h \
	idle-handles.h \
	idle-server-connection-iface.h \
	idle-server-connection.h \
	idle-dns-resolver.h \
	idle-ssl-server-connection.h \
	idle-parser.h \
	idle-im-factory.h \
	idle-connection-manager.c \
	idle-connection.c \
	idle-im-channel.c \
	idle-handles.c \
	idle-server-connection-iface.c \
	idle-server-connection.c \
	idle-dns-resolver.c \
	idle-ssl-server-connection.c \
	idle-parser.c \
	idle-im-factory.c \
	idle-version.h \
	text.h \
	text.c \
	$(BUILT_SOURCES)

libidle_convenience_la_SOURCES = \
    $(CORE_SOURCES)

telepathy_idle_SOURCES = \
	idle.c \
	idle.h

telepathy_idle_LDADD = libidle-convenience.la @TELEPATHY_LIBS@

noinst_LTLIBRARIES = libidle-convenience.la

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @SOFIA_CFLAGS@ @OPENSSL_CFLAGS@ @TELEPATHY_CFLAGS@ -std=c99
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @SOFIA_LIBS@ @OPENSSL_LIBS@ @TELEPATHY_LIBS@

# rule to generate the binding headers
%-glue.h: ../generate/xml-modified/%.xml Makefile
	dbus-binding-tool --mode=glib-server --output=$@ --prefix=$(subst -,_,$*) $<

%-signals-marshal.h: %-signals-marshal.list Makefile
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.h

%-signals-marshal.c: %-signals-marshal.list Makefile
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.c

%-marshal.h: %-marshal.list Makefile
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.h

%-marshal.c: %-marshal.list Makefile
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.c

config.h:
	echo '#define TELEPATHY_IDLE_VERSION "'@TELEPATHY_IDLE_VERSION@'"' > config.h
