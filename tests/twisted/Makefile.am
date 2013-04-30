TWISTED_TESTS = \
		cm/protocol.py \
		connect/connect-close-ssl.py \
		connect/connect-success.py \
		connect/connect-success-ssl.py \
		connect/connect-reject-ssl.py \
		connect/connect-fail.py \
		connect/connect-fail-ssl.py \
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
		messages/invalid-utf8.py \
		messages/messages-iface.py \
		messages/message-order.py \
		messages/leading-space.py \
		messages/long-message-split.py \
		messages/room-contact-mixup.py \
		messages/room-config.py \
		$(NULL)

TESTS =

TESTS_ENVIRONMENT = \
	PYTHONPATH=@abs_top_srcdir@/tests/twisted:@abs_top_builddir@/tests/twisted \
	IDLE_SSL_KEY=@abs_top_srcdir@/tests/twisted/tools/idletest.key \
	IDLE_SSL_CERT=@abs_top_srcdir@/tests/twisted/tools/idletest.cert

if WANT_TWISTED_TESTS

check-local: check-twisted

check-twisted:
	$(MAKE) -C tools
	rm -f tools/core
	rm -f tools/idle-testing.log
	failed=0; \
	sh $(srcdir)/tools/with-session-bus.sh \
		--config-file=tools/tmp-session-bus.conf \
		-- $(MAKE) check-TESTS \
		TESTS="$(TWISTED_TESTS)" \
		TESTS_ENVIRONMENT="$(TESTS_ENVIRONMENT) $(TEST_PYTHON) -u" || \
		failed=1; \
	if test -e tools/core; then\
		echo -e "\033[0;31;1mCore dump exists: tools/core\033[0m";\
		exit 1;\
	fi; \
	if test $$failed = 1; then\
		exit 1;\
	fi;

endif

EXTRA_DIST = \
	     $(TWISTED_TESTS) \
	     servicetest.py \
	     idletest.py \
	     constants.py \
	     $(NULL)

CLEANFILES = idle-[1-9]*.log *.pyc */*.pyc

SUBDIRS = tools
