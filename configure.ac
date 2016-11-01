#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.66])
AC_INIT(clr-debug-info, 33, arjan@linux.intel.com)
AM_INIT_AUTOMAKE([foreign -Wall -W subdir-objects])
AM_SILENT_RULES([yes])
AC_PROG_CC
AC_LANG(C)
AC_CONFIG_HEADERS([config.h])
PKG_CHECK_MODULES([curl], [libcurl])
PKG_CHECK_MODULES([fuse], [fuse])
PKG_CHECK_MODULES([SYSTEMD], [systemd])
LT_INIT

dir=""
AC_ARG_WITH([systemdsystemunitdir], AS_HELP_STRING([--with-systemdsystemunitdir=DIR],
	    [path to systemd system service dir @<:@default=/usr/lib/systemd/system@:>@]), [dir=${withval}],
	    [dir="$($PKG_CONFIG --variable=systemdsystemunitdir systemd)"])
test -z "${dir}" && dir=/usr/lib/systemd/system
AC_SUBST(systemdsystemunitdir, [${dir}])

dir=""
AC_ARG_WITH([systemdtmpfilesdir], AS_HELP_STRING([--with-systemdtmpfilesdir=DIR],
	    [path to systemd tmpfiles dir @<:@default=/usr/lib/tmpfiles.d@:>@]), [dir=${withval}],
	    [dir="$($PKG_CONFIG --variable=tmpfilesdir systemd)"])
test -z "${dir}" && dir=/usr/lib/tmpfiles.d
AC_SUBST(tmpfilesdir, [${dir}])

AC_CHECK_HEADER([stdatomic.h], [have_atomics="yes"], [have_atomics="no"])
if test x$have_atomics = "xyes"; then
        AC_DEFINE([HAVE_ATOMIC_SUPPORT], [1], [stdatomic supported by compiler])
else
        AC_MSG_WARN([C11 stdatomic support unavailable. Falling back to slow mutex])
fi

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

AC_MSG_RESULT([
        clr-debuginfo $VERSION

        prefix:                 ${prefix}
        libdir:                 ${libdir}
        sysconfdir:             ${sysconfdir}
        exec_prefix:            ${exec_prefix}
        bindir:                 ${bindir}
        datarootdir:            ${datarootdir}

        compiler:               ${CC}
        cflags:                 ${CFLAGS}
        ldflags:                ${LDFLAGS}

        systemd-unit-dir:       ${systemdsystemunitdir}
        tmpfiles.d:             ${tmpfilesdir}

        C11 stdatomic support:  ${have_atomics}
])
