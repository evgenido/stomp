AC_INIT([stomp], [0.1], [evgeni_dobrev@developer.bg])
AM_INIT_AUTOMAKE([-Wall -Werror foreign 1.11])
AM_SILENT_RULES([yes])
LT_INIT
AC_PROG_CC
PKG_CHECK_MODULES(CHECK,[check], [HAVE_CHECK=yes], [HAVE_CHECK=no])
AM_CONDITIONAL(HAVE_CHECK, test x$HAVE_CHECK = xyes)

AC_SUBST([STOMP_SO_VERSION], [0:0:0])
AC_SUBST([STOMP_API_VERSION], [0.0])

AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile stomp.pc])
AC_OUTPUT
