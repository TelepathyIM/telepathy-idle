tools_dir = $(top_srcdir)/tools

EXTRA_DIST = \
    all.xml \
    Connection_Interface_Renaming.xml

noinst_LTLIBRARIES = libidle-extensions.la

libidle_extensions_la_SOURCES = \
    extensions.h

nodist_libidle_extensions_la_SOURCES = \
    _gen/signals-marshal.c \
    _gen/signals-marshal.h \
    _gen/signals-marshal.list \
    _gen/enums.h \
    _gen/interfaces.h \
    _gen/svc.h \
    _gen/svc-Connection_Interface_Renaming.c \
    _gen/svc-Connection_Interface_Renaming.h \
    _gen/svc-Connection_Interface_Renaming-signals-marshal.list \
    _gen/svc-Connection_Interface_Renaming-glue.h

BUILT_SOURCES = $(nodist_libidle_extensions_la_SOURCES) extensions.html

CLEANFILES = $(BUILT_SOURCES) _gen/.exists

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @TELEPATHY_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @TELEPATHY_LIBS@

# Generated stuff

DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
XSLTPROCFLAGS = --nonet --novalid

_gen/.exists:
	$(mkdir_p) _gen
	touch $@

extensions.html: all.xml $(SPEC_INTERFACE_XMLS) $(tools_dir)/doc-generator.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude \
		$(tools_dir)/doc-generator.xsl \
		$(srcdir)/all.xml > $@

_gen/%.xml: %.xml $(tools_dir)/spec-to-introspect.xsl _gen/.exists
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(tools_dir)/spec-to-introspect.xsl $< \
		| $(DROP_NAMESPACE) > $@

_gen/async-%.xml: _gen/%.xml $(tools_dir)/make-all-async.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) $(tools_dir)/make-all-async.xsl $< > $@

_gen/svc-%-glue.h: _gen/async-%.xml
	$(DBUS_BINDING_TOOL) --mode=glib-server --output=$@ \
		--prefix=idle_svc_`echo $* | tr '[:upper:]' '[:lower:]'` $<

_gen/svc-%.c _gen/svc-%.h _gen/svc-%-signals-marshal.list: %.xml $(tools_dir)/genginterface.py _gen/.exists
	$(PYTHON) $(tools_dir)/genginterface.py \
		--filename=_gen/svc-$* --signal-marshal-prefix=_idle_ext \
		--include='<telepathy-glib/dbus.h>' \
		--include='"signals-marshal.h"' \
		--not-implemented-func='tp_dbus_g_method_return_not_implemented' \
		$< IdleSvc`echo $* | tr -d _`

_gen/signals-marshal.list: _gen/svc-Connection_Interface_Renaming-signals-marshal.list
	sort $^ /dev/null | uniq > $@

_gen/signals-marshal.h: _gen/signals-marshal.list
	$(GLIB_GENMARSHAL) --header --prefix=_idle_ext_marshal $< > $@

_gen/signals-marshal.c: _gen/signals-marshal.list
	$(GLIB_GENMARSHAL) --body --prefix=_idle_ext_marshal $< > $@

_gen/svc.h: Makefile.in _gen/.exists
	for i in Connection_Interface_Renaming; \
	do \
		echo "#include \"extensions/_gen/svc-$$i.h\""; \
	done > $@

_gen/enums.h: all.xml $(wildcard *.xml) $(tools_dir)/c-constants-generator.xsl _gen/.exists
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix Idle \
		$(tools_dir)/c-constants-generator.xsl \
		$(srcdir)/all.xml > $@

_gen/interfaces.h: all.xml $(wildcard *.xml) $(tools_dir)/c-interfaces-generator.xsl _gen/.exists
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix Idle \
		$(tools_dir)/c-interfaces-generator.xsl \
		$(srcdir)/all.xml > $@
