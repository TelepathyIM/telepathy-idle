TWISTED_TESTS = \
		cm/protocol.py \
		connect/connect-close-ssl.py \
		connect/connect-success.py \
		connect/connect-success-ssl.py \
		connect/connect-reject-ssl.py \
		connect/connect-fail.py \
		connect/connect-fail-ssl.py \
		connect/disconnect-before-socket-connected.py \
		connect/disconnect-during-cert-verification.py \
		connect/ping.py \
		connect/server-quit-ignore.py \
		connect/server-quit-noclose.py \
		connect/socket-closed-after-handshake.py \
		connect/socket-closed-during-handshake.py \
		connect/invalid-nick.py \
		contacts.py \
		channels/join-muc-channel.py \
		channels/join-muc-channel-bouncer.py \
		channels/requests-create.py \
		channels/requests-muc.py \
		channels/muc-channel-topic.py \
		channels/muc-destroy.py \
		channels/room-list-channel.py \
		channels/room-list-multiple.py \
		irc-command.py \
		messages/accept-invalid-nicks.py \
		messages/contactinfo-request.py \
		messages/invalid-utf8.py \
		messages/messages-iface.py \
		messages/message-order.py \
		messages/leading-space.py \
		messages/long-message-split.py \
		messages/room-contact-mixup.py \
		messages/room-config.py \
		$(NULL)

config.py: Makefile
	$(AM_V_GEN) { \
		echo "PACKAGE_STRING = \"$(PACKAGE_STRING)\""; \
	} > $@

if WANT_TWISTED_TESTS

check-local: check-twisted

CHECK_TWISTED_SLEEP=0

check-twisted: $(BUILT_SOURCES)
	$(MAKE) -C tools
	if test "x$(CHECK_TWISTED_SLEEP)" = x0; then \
		idle_test_sleep= ; \
	else \
		idle_test_sleep=--sleep=$(CHECK_TWISTED_SLEEP); \
	fi; \
	CHECK_TWISTED_UNINSTALLED=1 \
	  G_TEST_SRCDIR=@abs_srcdir@ \
	  G_TEST_BUILDDIR=@abs_builddir@ \
	  CHECK_TWISTED_SLEEP=$$idle_test_sleep \
	  ./run-test.sh "$(TWISTED_TESTS)"

endif

idle-twisted-tests.list: Makefile
	$(AM_V_GEN)echo $(TWISTED_TESTS) > $@

built_test_data = \
	config.py \
	idle-twisted-tests.list \
	$(NULL)

built_test_scripts = \
	run-test.sh \
	$(NULL)

BUILT_SOURCES = \
	$(built_test_data) \
	$(built_test_scripts) \
	$(NULL)

if ENABLE_INSTALLED_TESTS
testsdir = ${datadir}/telepathy-idle-tests
twistedtestsdir = ${testsdir}/twisted
nobase_dist_twistedtests_DATA = $(dist_test_data)
twistedtests_DATA = $(built_test_data)
twistedtests_SCRIPTS = $(built_test_scripts)
endif

run-test.sh: run-test.sh.in Makefile
	$(AM_V_GEN)sed \
			-e 's![@]idletestsdir[@]!${testsdir}!' \
			-e 's![@]TEST_PYTHON[@]!$(TEST_PYTHON)!' \
			< $< > $@.tmp && \
		chmod +x $@.tmp && \
		mv $@.tmp $@

dist_test_data = \
	$(TWISTED_TESTS) \
	constants.py \
	idletest.py \
	servicetest.py \
	$(NULL)

EXTRA_DIST = \
	$(dist_test_data) \
	run-test.sh.in \
	$(NULL)

CLEANFILES = \
	$(BUILT_SOURCES) \
	idle-[1-9]*.log \
	*.pyc \
	*/*.pyc \
	$(NULL)

SUBDIRS = tools
