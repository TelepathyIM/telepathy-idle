AC_PREREQ([2.59])

AC_INIT
AC_CONFIG_MACRO_DIR([m4])

AS_VERSION(telepathy-idle, TELEPATHY_IDLE_VERSION, 0, 1, 2, 1, WERROR="no", WERROR="no")

AM_INIT_AUTOMAKE($PACKAGE, $VERSION)

AM_PROG_LIBTOOL
AM_CONFIG_HEADER(config.h)

dnl check for tools
AC_PROG_CC
AC_PROG_CC_STDC
AM_PROG_AS

dnl decide on error flags
AS_COMPILER_FLAG(-Wall, WALL="yes", WALL="no")

if test "x$WALL" = "xyes"; then
  ERROR_CFLAGS="-Wall"

  if test "x$WERROR" = "xyes"; then
    AS_COMPILER_FLAG(-Werror,ERROR_CFLAGS="$ERROR_CFLAGS -Werror",ERROR_CFLAGS="$ERROR_CFLAGS")
  fi
fi

AC_SUBST(ERROR_CFLAGS)

AC_HEADER_STDC([])
AC_C_INLINE

dnl GTK docs
GTK_DOC_CHECK

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

dnl PKG_CHECK_MODULES(SOFIA_TOOLS, [sofia-tools >= 1.12],
dnl [
dnl     SOFIA_CFLAGS=$SOFIA_TOOLS_CFLAGS
dnl     SOFIA_LIBS=$SOFIA_TOOLS_LIBS
dnl ],
dnl [
dnl     PKG_CHECK_MODULES(SOFIA_SIP_UA, [sofia-sip-ua >= 1.12],
dnl         [PKG_CHECK_MODULES(SOFIA_SIP_UA_GLIB, [sofia-sip-ua-glib >= 1.12],
dnl         [
dnl             SOFIA_CFLAGS="${SOFIA_SIP_UA_CFLAGS}${SOFIA_SIP_UA_GLIB_CFLAGS}"
dnl             SOFIA_LIBS="${SOFIA_SIP_UA_LIBS}${SOFIA_SIP_UA_GLIB_LIBS}"
dnl         ]]))
dnl ])

AC_SUBST(SOFIA_CFLAGS)
AC_SUBST(SOFIA_LIBS)

PKG_CHECK_MODULES(TELEPATHY, [telepathy-glib >= 0.5.8])

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
DBUS_BINDING_TOOL=
AC_CHECK_PROGS([DBUS_BINDING_TOOL], [dbus-binding-tool])
if test -z "$DBUS_BINDING_TOOL"; then
  AC_MSG_ERROR([dbus-binding-tool (from dbus-glib) is required])
fi
PYTHON=
AC_CHECK_PROGS([PYTHON], [python2.3 python2.4 python2.5 python])
if test -z "$PYTHON"; then
  AC_MSG_ERROR([Python is required to compile this package])
fi

AS_AC_EXPAND(DATADIR, $datadir)
DBUS_SERVICES_DIR="$DATADIR/dbus-1/services"
AC_SUBST(DBUS_SERVICES_DIR)
AC_DEFINE_UNQUOTED(DBUS_SERVICES_DIR, "$DBUS_SERVICES_DIR", [DBus services directory])

AC_OUTPUT( Makefile \
					 data/Makefile \
					 extensions/Makefile \
					 extensions/tools/Makefile \
           m4/Makefile \
           src/Makefile \
					 tests/Makefile \
)
