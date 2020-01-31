#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.66])
AC_INIT(clr-debug-info, 51, arjan@linux.intel.com)
AM_INIT_AUTOMAKE([foreign -Wall -W subdir-objects])
AM_SILENT_RULES([yes])
AC_PROG_CC
AM_PROG_AR
AC_LANG(C)
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([clr_debug_daemon.socket])
AC_CONFIG_FILES([debuginfo.conf])
PKG_CHECK_MODULES([curl], [libcurl])
PKG_CHECK_MODULES([fuse3], [fuse3])
PKG_CHECK_MODULES([SYSTEMD], [systemd])
PKG_CHECK_MODULES([LIBSYSTEMD], [libsystemd])
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

SOCKET_PATH=""
AC_ARG_WITH([socket-path], AS_HELP_STRING([--with-socket-path=SOCKET_PATH],
            [path to create unix socket @<:@default=/run/clr-debug-info@:>@]), [SOCKET_PATH=${withval}],
            [SOCKET_PATH="/run/clr-debug-info"])
test -z "${SOCKET_PATH}" && SOCKET_PATH=/run/clr-debug-info
AC_DEFINE_UNQUOTED([SOCKET_PATH], ["${SOCKET_PATH}"], [path to create unix socket @<:@default=/run/clr-debug-info@:>@])
AC_SUBST(SOCKET_PATH, [${SOCKET_PATH}])

CACHE_DIR=""
AC_ARG_WITH([cache-dir], AS_HELP_STRING([--with-cache-dir=CACHE_DIR],
            [path to cache downloaded content @<:@default=/var/cache/debuginfo@:>@]), [CACHE_DIR="${withval}"],
            [CACHE_DIR="/var/cache/debuginfo"])
test -z "${CACHE_DIR}" && CACHE_DIR=/var/cache/debuginfo
AC_DEFINE_UNQUOTED([CACHE_DIR], ["${CACHE_DIR}"], [path to cache downloaded content @<:@default=/var/cache/debuginfo@:>@])
AC_SUBST(CACHE_DIR, [${CACHE_DIR}])

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
        socket_dir:             ${SOCKET_PATH}
        cache_dir:              ${CACHE_DIR}

        C11 stdatomic support:  ${have_atomics}
])
