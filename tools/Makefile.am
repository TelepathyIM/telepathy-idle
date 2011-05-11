EXTRA_DIST = \
    c-constants-generator.xsl \
    c-interfaces-generator.xsl \
    doc-generator.xsl \
    glib-ginterface-gen.py \
    glib-signals-marshal-gen.py \
    identity.xsl \
    libglibcodegen.py \
    make-release-mail.py

CLEANFILES = libglibcodegen.pyc libglibcodegen.pyo

glib-ginterface-gen.py: libglibcodegen.py
	touch $@

glib-signals-marshal-gen.py: libglibcodegen.py
	touch $@
