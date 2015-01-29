EXTRA_DIST = COPYING clr_debug_fuse.service clr_debug_daemon.service debuginfo.conf

bin_PROGRAMS = clr_debug_fuse clr_debug_daemon clr_debug_prepare

clr_debug_fuse_SOURCES = src/fuse.c src/client.c

clr_debug_daemon_SOURCES = src/server.c

clr_debug_prepare_SOURCES = src/prepare.c


AM_CPPFLAGS = $(AM_CFLAGS) $(glib_CFLAGS) ${curl_CFLAGS} ${fuse_CFLAGS}
clr_debug_fuse_LDADD = ${fuse_LIBS} $(glib_LIBS)
clr_debug_daemon_LDADD = $(glib_LIBS) ${curl_LIBS}
clr_debug_prepare_LDADD = $(glib_LIBS)

systemdsystemunit_DATA = clr_debug_fuse.service clr_debug_daemon.service

tmpfiles_DATA = debuginfo.conf
