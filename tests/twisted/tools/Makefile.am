exec-with-log.sh: exec-with-log.sh.in
	sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" \
		-e "s|[@]abs_top_srcdir[@]|@abs_top_srcdir@|g" $< > $@
	chmod +x $@

%.conf: %.conf.in
	sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@

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

EXTRA_DIST = \
	$(service_in_files) \
	$(conf_in_files) \
	exec-with-log.sh.in \
	idletest.cert \
	idletest.key \
	with-session-bus.sh

CLEANFILES = \
    $(BUILT_SOURCES) \
    idle-testing.log
