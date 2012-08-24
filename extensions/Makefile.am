tools_dir = $(top_srcdir)/tools

EXTRA_DIST = \
    all.xml \
    Connection_Interface_Renaming.xml

noinst_LTLIBRARIES = libidle-extensions.la

libidle_extensions_la_SOURCES = \
    extensions.h

nodist_libidle_extensions_la_SOURCES = \
    _gen/enums.h \
    _gen/interfaces.h \
    _gen/svc-gtk-doc.h \
    _gen/svc.h \
    _gen/svc.c

BUILT_SOURCES = \
    _gen/all.xml \
    $(nodist_libidle_extensions_la_SOURCES) \
    extensions.html

CLEANFILES = $(BUILT_SOURCES)

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @TELEPATHY_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @TELEPATHY_LIBS@

# Generated stuff

DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
XSLTPROCFLAGS = --nonet --novalid

_gen/all.xml: all.xml $(wildcard $(srcdir)/*.xml)
	@$(mkdir_p) _gen
	$(AM_V_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) \
		--xinclude $(tools_dir)/identity.xsl \
		$< > $@

extensions.html: _gen/all.xml $(tools_dir)/doc-generator.xsl
	$(AM_V_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(tools_dir)/doc-generator.xsl \
		$< > $@

_gen/svc.h: _gen/svc.c
_gen/svc-gtk-doc.h: _gen/svc.c

_gen/svc.c: _gen/all.xml \
	$(tools_dir)/glib-ginterface-gen.py
	$(AM_V_GEN)$(PYTHON) $(tools_dir)/glib-ginterface-gen.py \
		--filename=_gen/svc --signal-marshal-prefix=_idle_ext \
		--include='<telepathy-glib/dbus.h>' \
		--not-implemented-func='tp_dbus_g_method_return_not_implemented' \
		--allow-unstable \
		$< Idle_Svc_

_gen/enums.h: _gen/all.xml $(tools_dir)/c-constants-generator.xsl
	$(AM_V_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix Idle \
		$(tools_dir)/c-constants-generator.xsl \
		$< > $@

_gen/interfaces.h: _gen/all.xml $(tools_dir)/c-interfaces-generator.xsl
	$(AM_V_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix Idle \
		$(tools_dir)/c-interfaces-generator.xsl \
		$< > $@
