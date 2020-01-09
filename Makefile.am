EXTRA_DIST = COPYING clr_debug_fuse.service clr_debug_daemon.service clr_debug_daemon.socket debuginfo.conf

DISTCHECK_CONFIGURE_FLAGS = \
	--with-systemdsystemunitdir=$$dc_install_base/$(systemdsystemunitdir) \
	--with-systemdtmpfilesdir=$$dc_install_base/$(tmpfilesdir)

AM_CFLAGS = \
        -fstack-protector -Wall -pedantic \
        -Wstrict-prototypes -Wundef -fno-common \
        -Werror-implicit-function-declaration \
        -Wformat -Wformat-security -Werror=format-security \
        -Wno-conversion -Wunused-variable -Wunreachable-code \
        -Wall -W -D_FORTIFY_SOURCE=2 -std=c11

AM_CPPFLAGS = $(AM_CFLAGS) ${curl_CFLAGS} ${fuse_CFLAGS}

bin_PROGRAMS = clr_debug_fuse clr_debug_daemon

dist_bin_SCRIPTS = scripts/clr_debug_prepare

noinst_LTLIBRARIES = \
	libnica.la

# Extract of libnica components
libnica_la_SOURCES = \
	src/nica/files.c \
	src/nica/files.h \
	src/nica/hashmap.c \
	src/nica/hashmap.h \
	src/nica/util.h


clr_debug_fuse_SOURCES = src/fuse.c src/client.c

clr_debug_daemon_SOURCES = src/server.c
clr_debug_daemon_CFLAGS = \
	-pthread \
	$(AM_CFLAGS) \
	$(LIBSYSTEMD_CFLAGS)


clr_debug_fuse_LDADD = ${fuse_LIBS} libnica.la
clr_debug_daemon_LDADD = ${curl_LIBS} libnica.la ${LIBSYSTEMD_LIBS}

systemdsystemunit_DATA = clr_debug_fuse.service clr_debug_daemon.service clr_debug_daemon.socket

tmpfiles_DATA = debuginfo.conf

# Functional Testing
noinst_PROGRAMS = testing_fuse testing_daemon

testing_fuse_SOURCES = tests/testing_fuse.c
testing_fuse_CFLAGS = $(AM_CFLAGS)

testing_daemon_SOURCES = tests/testing_daemon.c
testing_daemon_CFLAGS = $(AM_CFLAGS)
