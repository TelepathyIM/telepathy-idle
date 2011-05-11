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
m4_define([idle_micro_version], [10])
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

dnl decide error flags
AS_COMPILER_FLAG(-Wall, ERROR_CFLAGS="-Wall", ERROR_CFLAGS="")
AS_COMPILER_FLAG(-Werror, werror=yes, werror=no)
AS_COMPILER_FLAG(-Wextra, wextra=yes, wextra=no)
AS_COMPILER_FLAG(-Wno-missing-field-initializers,
        wno_missing_field_initializers=yes,
        wno_missing_field_initializers=no)
AS_COMPILER_FLAG(-Wno-unused-parameter,
        wno_unused_parameter=yes,
        wno_unused_parameter=no)

ifelse(idle_nano_version, 0, [],
    [
        if test x$werror = xyes; then
            ERROR_CFLAGS="$ERROR_CFLAGS -Werror"
        fi
        if test x$wextra = xyes -a \
            x$wno_missing_field_initializers = xyes -a \
            x$wno_unused_parameter = xyes; then
            ERROR_CFLAGS="$ERROR_CFLAGS -Wextra -Wno-missing-field-initializers -Wno-unused-parameter"
        fi
    ])

AC_SUBST(ERROR_CFLAGS)

AC_HEADER_STDC([])
AC_C_INLINE

dnl Check for Glib 
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.4, gobject-2.0 >= 2.4])

AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

GLIB_GENMARSHAL=`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

dnl Check for D-Bus
PKG_CHECK_MODULES(DBUS, [dbus-1 >= 0.51, dbus-glib-1 >= 0.51])

AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

PKG_CHECK_MODULES(TELEPATHY, [telepathy-glib >= 0.13.10])

AC_SUBST(TELEPATHY_CFLAGS)
AC_SUBST(TELEPATHY_LIBS)

dnl Check for OpenSSL
PKG_CHECK_MODULES(OPENSSL, [openssl >= 0.9.7])

AC_SUBST(OPENSSL_CFLAGS)
AC_SUBST(OPENSSL_LIBS)

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
