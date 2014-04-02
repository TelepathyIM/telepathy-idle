exec-with-log.sh: exec-with-log.sh.in
	sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" \
		-e "s|[@]abs_top_srcdir[@]|@abs_top_srcdir@|g" $< > $@
	chmod +x $@

servicedir = $(DBUS_SERVICES_DIR)

%.conf: %.conf.in
	@$(MKDIR_P) installed
	sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" \
		-e "s|[@]servicedir[@]|$(servicedir)|g" \
		$< > $@

# We don't use the full filename for the .in because > 99 character filenames
# in tarballs are non-portable (and automake 1.8 doesn't let us build
# non-archaic tarballs)
im.telepathy.v1.ConnectionManager.%.service: %.service.in
	sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@

# D-Bus service file for testing
service_in_files = idle.service.in
service_files = im.telepathy.v1.ConnectionManager.idle.service

# D-Bus config file for testing
conf_in_files = tmp-session-bus.conf.in
conf_files = $(conf_in_files:.conf.in=.conf)

BUILT_SOURCES = $(service_files) $(conf_files) exec-with-log.sh

dist_test_scripts = \
	with-session-bus.sh \
	$(NULL)

dist_test_data = \
	idletest.cert \
	idletest.key \
	$(NULL)

built_test_data = \
	installed/tmp-session-bus.conf \
	$(NULL)

EXTRA_DIST = \
	$(dist_test_data) \
	$(dist_test_scripts) \
	$(service_in_files) \
	$(conf_in_files) \
	installed/tmp-session-bus.conf.in \
	exec-with-log.sh.in \
	$(NULL)

CLEANFILES = \
    $(BUILT_SOURCES) \
    idle-testing.log

if ENABLE_INSTALLED_TESTS
testsdir = ${datadir}/telepathy-idle-tests
twistedtestsdir = ${testsdir}/twisted
twistedtoolsdir = ${twistedtestsdir}/tools
dist_twistedtools_SCRIPTS = $(dist_test_scripts)
dist_twistedtools_DATA = $(dist_test_data)
twistedtools_DATA = $(built_test_data)
endif
