AC_PREREQ([2.59])

# Making releases:
#   set the new version number:
#     odd minor -> development series
#     even minor -> stable series
#     increment micro for each release within a series
#   set nano_version to 0
#   make the release, tag it
#   set nano_version to 1

m4_define([idle_major_version], [0])
m4_define([idle_minor_version], [1])
m4_define([idle_micro_version], [11])
m4_define([idle_nano_version], [1])

m4_define([idle_base_version],
          [idle_major_version.idle_minor_version.idle_micro_version])
m4_define([idle_version],
          [m4_if(idle_nano_version, 0,
                 [idle_base_version],
                 [idle_base_version].[idle_nano_version])])

AC_INIT([telepathy-idle], [idle_version],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=Telepathy&component=idle])

AC_CONFIG_MACRO_DIR([m4])

# Enable silent build rules by default, requires at least
# Automake-1.11. Disable by either passing --disable-silent-rules to
# configure or passing V=1 to make
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AM_INIT_AUTOMAKE([1.9 -Wno-portability])

AM_PROG_LIBTOOL
AM_CONFIG_HEADER(config.h)
AM_PROG_MKDIR_P

dnl check for tools
AC_PROG_CC
AC_PROG_CC_STDC
AM_PROG_AS

AS_IF([test idle_nano_version = 0],
[ # version x.y.z - "official release",
  # disable extra checks by default
  AC_ARG_ENABLE([fatal-warnings],
    [AC_HELP_STRING([--enable-fatal-warnings],
                    [make various warnings fatal])],
    [],
    [enable_fatal_warnings=no])
],
[ # version x.y.z.1 - development snapshot,
  # enable extra checks by default
  AC_ARG_ENABLE([fatal-warnings],
    [AC_HELP_STRING([--disable-fatal-warnings],
                    [make various warnings non-fatal])],
    [],
    [enable_fatal_warnings=yes])
])

TP_COMPILER_WARNINGS([ERROR_CFLAGS], [test "x$enable_fatal_warnings" = xyes],
  [all \
   extra \
   declaration-after-statement \
   shadow \
   missing-prototypes \
   nested-externs \
   pointer-arith \
   sign-compare \
   strict-prototypes \
   format-security \
   init-self],
  [missing-field-initializers \
   unused-parameter])
AC_SUBST([ERROR_CFLAGS])

# these aren't really error flags but they serve a similar purpose for us -
# making the toolchain stricter
if test "x$enable_fatal_warnings" = xyes; then
  TP_ADD_LINKER_FLAG([ERROR_LDFLAGS], [-Wl,--no-copy-dt-needed-entries])
fi

AC_HEADER_STDC([])
AC_C_INLINE

AC_DEFINE(GLIB_VERSION_MIN_REQUIRED, GLIB_VERSION_2_28, [Ignore post 2.28 deprecations])
AC_DEFINE(GLIB_VERSION_MAX_ALLOWED, GLIB_VERSION_2_30, [Prevent post 2.30 APIs])

PKG_CHECK_MODULES([GLIB],
  [glib-2.0 >= 2.30.0, gobject-2.0 >= 2.30.0, gio-2.0 >= 2.30.0 ])

PKG_CHECK_MODULES([DBUS], [dbus-1 >= 0.51, dbus-glib-1 >= 0.51])

PKG_CHECK_MODULES([TELEPATHY], [telepathy-glib >= 0.15.9])

PKG_CHECK_MODULES([OPENSSL], [openssl >= 0.9.7])

dnl Check for code generation tools
XSLTPROC=
AC_CHECK_PROGS([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([xsltproc (from the libxslt source package) is required])
fi

AM_PATH_PYTHON([2.3])

dnl check for a version of python that can run the twisted tests
AC_MSG_CHECKING([for Python with Twisted and IRC protocol support])
for TEST_PYTHON in python2.5 python2.6 python; do
  if $TEST_PYTHON -c "from sys import version_info; import dbus, dbus.mainloop.glib; raise SystemExit(version_info < (2, 5, 0, 'final', 0))" >/dev/null 2>&1; then
    if $TEST_PYTHON -c "import twisted.words.protocols.irc, twisted.internet.reactor" >/dev/null 2>&1; then
      AM_CONDITIONAL([WANT_TWISTED_TESTS], true)
      break
    else
      TEST_PYTHON=no
    fi
  fi
done
AC_MSG_RESULT([$TEST_PYTHON])
AC_SUBST(TEST_PYTHON)
AM_CONDITIONAL([WANT_TWISTED_TESTS], test xno != x$TEST_PYTHON)


AS_AC_EXPAND(DATADIR, $datadir)
DBUS_SERVICES_DIR="$DATADIR/dbus-1/services"
AC_SUBST(DBUS_SERVICES_DIR)
AC_DEFINE_UNQUOTED(DBUS_SERVICES_DIR, "$DBUS_SERVICES_DIR", [DBus services directory])

AC_CHECK_FUNCS(strnlen)

AC_OUTPUT( Makefile \
					 data/Makefile \
					 extensions/Makefile \
           m4/Makefile \
           src/Makefile \
					 tests/Makefile \
						 tests/twisted/Makefile \
							 tests/twisted/tools/Makefile \
					 tools/Makefile \
)
