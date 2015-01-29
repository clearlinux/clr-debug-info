#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.66])
AC_INIT(clr-debug-info, 11, arjan@linux.intel.com)
AM_INIT_AUTOMAKE([foreign -Wall -W subdir-objects])
AM_SILENT_RULES([yes])
AC_PROG_CC
AC_LANG(C)
AC_CONFIG_HEADERS([config.h])
PKG_CHECK_MODULES([glib], [glib-2.0 gthread-2.0])
PKG_CHECK_MODULES([curl], [libcurl])
PKG_CHECK_MODULES([fuse], [fuse])
PKG_CHECK_MODULES([SYSTEMD], [systemd])

tmpfilesdir=`pkg-config --variable tmpfilesdir systemd`
AC_SUBST(tmpfilesdir)

systemdsystemunitdir=`pkg-config --variable systemdsystemunitdir systemd`
AC_SUBST(systemdsystemunitdir)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
