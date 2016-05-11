EXTRA_DIST = COPYING clr_debug_fuse.service clr_debug_daemon.service debuginfo.conf

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

bin_PROGRAMS = clr_debug_fuse clr_debug_daemon clr_debug_prepare

noinst_LTLIBRARIES = \
	libnica.la

# Extract of libnica components
libnica_la_SOURCES = \
	src/nica/macros.h \
	src/nica/hashmap.h \
	src/nica/hashmap.c \
	src/nica/files.h \
	src/nica/files.c

clr_debug_fuse_SOURCES = src/fuse.c src/client.c

clr_debug_daemon_SOURCES = src/server.c
clr_debug_daemon_CFLAGS = \
	-pthread \
	$(AM_CFLAGS)

clr_debug_prepare_SOURCES = src/prepare.c


clr_debug_fuse_LDADD = ${fuse_LIBS} libnica.la
clr_debug_daemon_LDADD = ${curl_LIBS} libnica.la

systemdsystemunit_DATA = clr_debug_fuse.service clr_debug_daemon.service

tmpfiles_DATA = debuginfo.conf
