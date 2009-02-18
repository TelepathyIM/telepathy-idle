ACLOCAL_AMFLAGS = -I m4

SUBDIRS = tools extensions src data m4 tests

check-twisted : all
	$(MAKE) -C tests/twisted check-twisted

check-all: check check-twisted

include tools/telepathy.am
