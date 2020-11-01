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
		messages/accept-invalid-nicks.py \
		messages/contactinfo-request.py \
		messages/messages-iface.py \
		messages/message-order.py \
		messages/leading-space.py \
		messages/long-message-split.py \
		messages/room-contact-mixup.py \
		messages/room-config.py \
		$(NULL)

# TODO: Fix messages/invalid-utf8.py

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
	IDLE_TEST_UNINSTALLED=1 \
	  IDLE_ABS_TOP_SRCDIR=@abs_top_srcdir@ \
	  IDLE_ABS_TOP_BUILDDIR=@abs_top_builddir@ \
	  IDLE_TEST_SLEEP=$$idle_test_sleep \
	  ./run-test.sh "$(TWISTED_TESTS)"

idle-twisted-tests.list: Makefile
	$(AM_V_GEN)echo $(TWISTED_TESTS) > $@

BUILT_SOURCES = \
	config.py \
	idle-twisted-tests.list \
	run-test.sh \
	$(NULL)

# We don't really use idletestsdir yet - we only support uninstalled testing
# so far - but I'm substituting it to keep the script more similar to Gabble's.
# ${pkglibexecdir}/tests is what GNOME's InstalledTests goal recommends.
run-test.sh: run-test.sh.in Makefile
	$(AM_V_GEN)sed \
			-e 's![@]idletestsdir[@]!${pkglibexecdir}/tests!' \
			-e 's![@]TEST_PYTHON[@]!$(TEST_PYTHON)!' \
			< $< > $@.tmp && \
		chmod +x $@.tmp && \
		mv $@.tmp $@

endif

EXTRA_DIST = \
	     $(TWISTED_TESTS) \
	     run-test.sh.in \
	     servicetest.py \
	     idletest.py \
	     constants.py \
	     $(NULL)

CLEANFILES = \
	$(BUILT_SOURCES) \
	idle-[1-9]*.log \
	*.pyc \
	*/*.pyc \
	$(NULL)

SUBDIRS = tools
