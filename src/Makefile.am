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
	room-config.c \
	room-config.h \
	idle-parser.c \
	idle-parser.h \
	protocol.c \
	protocol.h \
	idle-roomlist-channel.h \
	idle-roomlist-channel.c \
	idle-roomlist-manager.h \
	idle-roomlist-manager.c \
	idle-server-connection.c \
	idle-server-connection.h \
	idle-text.h \
	idle-text.c \
	server-tls-channel.c \
	server-tls-channel.h \
	server-tls-manager.c \
	server-tls-manager.h \
	tls-certificate.c \
	tls-certificate.h \
	$(NULL)

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
	@TELEPATHY_CFLAGS@ \
	-std=c99

AM_LDFLAGS = \
	$(ERROR_LDFLAGS) \
	$(NULL)

ALL_LIBS = \
	@DBUS_LIBS@ \
	@GLIB_LIBS@ \
	@TELEPATHY_LIBS@

man_MANS = telepathy-idle.8

EXTRA_DIST = \
	telepathy-idle.8.in \
	meson.build \
	$(NULL)

%.8: %.8.in Makefile
	$(AM_V_GEN)sed -e 's,[@]libexecdir[@],@libexecdir@,' < $< > $@
