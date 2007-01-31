BUILT_SOURCES = \
		idle-connection-manager-glue.h \
		idle-connection-manager-signals-marshal.h \
		idle-connection-manager-signals-marshal.c \
		idle-connection-glue.h \
		idle-connection-signals-marshal.h \
		idle-connection-signals-marshal.c \
		idle-im-channel-glue.h \
		idle-im-channel-signals-marshal.h \
		idle-im-channel-signals-marshal.c \
		idle-muc-channel-glue.h \
		idle-muc-channel-signals-marshal.h \
		idle-muc-channel-signals-marshal.c \
		telepathy-errors-enumtypes.h \
		telepathy-errors-enumtypes.c \
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
	idle-muc-channel.h \
    idle-handles.h \
    idle-handle-set.h \
	idle-handles-private.h \
    telepathy-errors.h \
    telepathy-helpers.h \
	idle-server-connection-iface.h \
	idle-server-connection.h \
	idle-server-connection-util.h \
	idle-dns-resolver.h \
	idle-ssl-server-connection.h \
    idle-connection-manager.c \
    idle-connection.c \
    idle-im-channel.c \
	idle-muc-channel.c \
    idle-handles.c \
    idle-handle-set.c \
    telepathy-errors.c \
    telepathy-helpers.c \
	idle-server-connection-iface.c \
	idle-server-connection.c \
	idle-server-connection-util.c \
	idle-dns-resolver.c \
	idle-ssl-server-connection.c \
	idle-version.h \
    $(BUILT_SOURCES)

libidle_convenience_la_SOURCES = \
    $(CORE_SOURCES)

telepathy_idle_SOURCES = \
    idle.c \
	idle.h

telepathy_idle_LDADD = libidle-convenience.la

noinst_LTLIBRARIES = libidle-convenience.la

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @SOFIA_CFLAGS@ @OPENSSL_CFLAGS@ @TELEPATHY_CFLAGS@
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


# rules for making the glib enum objects
%-enumtypes.h: %.h Makefile
	glib-mkenums \
	--fhead "#ifndef __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n#define __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define $(shell echo $* | tr [:lower:]- [:upper:]_ | sed 's/_.*//')_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__ */" \
	$< > $@

%-enumtypes.c: %.h Makefile
	glib-mkenums \
	--fhead "#include <$*.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@VALUENAME@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$< > $@

config.h:
	echo '#define TELEPATHY_IDLE_VERSION "'@TELEPATHY_IDLE_VERSION@'"' > config.h
