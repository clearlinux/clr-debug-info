#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.66])
AC_INIT(clr-debug-info, 7, arjan@linux.intel.com)
AM_INIT_AUTOMAKE([foreign -Wall -W])
AM_SILENT_RULES([yes])
AC_PROG_CC
AC_LANG(C)
AC_CONFIG_HEADERS([config.h])
PKG_CHECK_MODULES([glib], [glib-2.0 gthread-2.0])
PKG_CHECK_MODULES([curl], [libcurl])
PKG_CHECK_MODULES([fuse], [fuse])
PKG_CHECK_MODULES([SYSTEMD], [systemd])

AC_ARG_WITH([systemdunitdir], AC_HELP_STRING([--with-systemdunitdir=DIR],
	[path to systemd service directory]), [path_systemdunit=${withval}],
	[path_systemdunit="`$PKG_CONFIG --variable=systemdsystemunitdir systemd`"])
if (test -n "${path_systemdunit}"); then
SYSTEMD_UNITDIR="${path_systemdunit}"
AC_SUBST(SYSTEMD_UNITDIR)
AM_CONDITIONAL(SYSTEMD, test -n "${path_systemdunit}")
fi

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
