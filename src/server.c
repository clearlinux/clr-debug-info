/*
 *   Clear Linux -- automatic debug information installation
 *
 *      Copyright (C) 2013  Arjan van de Ven
 * 	Curl portions borrowed from the Fenrus Update code
 *          which in part is (C) 2012 Intel Corporation
 *      Copyright (C) 2014  Intel Corporation
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, version 3 or later of the License.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Authors:
 *         Arjan van de Ven <arjanvandeven@gmail.com>
 *         Ikey Doherty <michael.i.doherty@intel.com>
 *
 */

#define _GNU_SOURCE

#include <errno.h>
#include <grp.h>
#include <linux/capability.h>
#include <malloc.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "nica/files.h"
#include "nica/hashmap.h"

#include <curl/curl.h>

#include "config.h"

#ifdef HAVE_ATOMIC_SUPPORT
#include <stdatomic.h>
#endif

static pthread_mutex_t dupes_mutex = PTHREAD_MUTEX_INITIALIZER;

char *urls[2] = { "https://cdn.download.clearlinux.org/debuginfo/",
                  "https://cdn.download.clearlinux.org/debuginfo/" };
int urlcounter = 1;

static NcHashmap *hash = NULL;

#define MAX_CONNECTIONS 16

static int avoid_dupes(const char *url)
{
        int retval = 0;
        void *value;
        pthread_mutex_lock(&dupes_mutex);

        if (hash == NULL) {
                hash = nc_hashmap_new_full(nc_string_hash, nc_string_compare, free, NULL);
        }

        if (nc_hashmap_ensure_get(hash, url, &value)) {
                unsigned long tm;
                tm = (unsigned long)value;
                if (time(NULL) - tm < 600) {
                        retval = 1;
                }
        } else {
                unsigned long tm;
                tm = time(NULL);
                void *data = (void *)(unsigned long)tm;
                nc_hashmap_put(hash, strdup(url), data);
        }

        pthread_mutex_unlock(&dupes_mutex);
        return retval;
}

#ifdef HAVE_ATOMIC_SUPPORT

static atomic_int current_connection_count = 0;

/**
 * Get the current connection count atomically
 */
__nc_inline__ static inline int get_current_connection_count(void)
{
        return atomic_load(&current_connection_count);
}

/**
 * Increment the connection counter atomically
 */
__nc_inline__ static inline void inc_connection_count(void)
{
        atomic_fetch_add(&current_connection_count, 1);
}

/**
 * Decrement the connection counter atomically
 */
__nc_inline__ static inline void dec_connection_count(void)
{
        atomic_fetch_sub(&current_connection_count, 1);
}
#else /* HAVE_ATOMIC_SUPPORT */

static int current_connection_count = 0;

/* No stdatomic compiler support, fallback to pthread mutex (slower) */
pthread_mutex_t con_count_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Get the current connection count via mutex
 */
__nc_inline__ static inline int get_current_connection_count(void)
{
        int r;
        pthread_mutex_lock(&con_count_mutex);
        r = current_connection_count;
        pthread_mutex_unlock(&con_count_mutex);
        return r;
}

/**
 * Increment the connection counter via mutex
 */
__nc_inline__ static inline void inc_connection_count(void)
{
        pthread_mutex_lock(&con_count_mutex);
        current_connection_count++;
        pthread_mutex_unlock(&con_count_mutex);
}

/**
 * Decrement the connection counter via mutex
 */
__nc_inline__ static inline void dec_connection_count(void)
{
        pthread_mutex_lock(&con_count_mutex);
        current_connection_count--;
        pthread_mutex_unlock(&con_count_mutex);
}

#endif /* !(HAVE_ATOMIC_SUPPORT) */

static int curl_get_file(const char *url, const char *prefix, time_t timestamp)
{
        CURLcode code;
        long ret;
        long changed;
        int fd;
        char filename[PATH_MAX];
        CURL *curl = NULL;
        FILE *file;
        struct stat statbuf;

        if (avoid_dupes(url)) {
                return 300;
        }

        curl = curl_easy_init();
        if (curl == NULL) {
                return 301;
        }

        strcpy(filename, "/tmp/clr-debug-info-XXXXXX");

        fd = mkstemp(filename);
        if (fd < 0) {
                curl_easy_cleanup(curl);
                return 500;
        }

        file = fdopen(fd, "w");

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);

        /*
         * Some sane timeout values to prevent stalls
         *
         * Connect timeout is for first connection to the server.
         * The low speed timeout is for slow downloads. Since many
         * files are several mB large, we want to prevent them from
         * taking forever. (1kB/sec avg over 30secs).
         */
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024);

        /* request timestamp of files from server */
        curl_easy_setopt(curl, CURLOPT_FILETIME, 1);

        if (timestamp) {
                curl_easy_setopt(curl, CURLOPT_TIMECONDITION, CURL_TIMECOND_IFMODSINCE);
                curl_easy_setopt(curl, CURLOPT_TIMEVALUE, timestamp);
        }

        code = curl_easy_perform(curl);

        /* can't trust the file if we get an error back */
        if (code != 0) {
                unlink(filename);
        }

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);
        fflush(file);
        //	printf("HTTP return code is %i\n", ret);

        /* HTTP 304 is returned if (a) the cached debuginfo has the same
         * timestamp or is newer than that on the server and (b) we haven't
         * already added the URL to the hash table. So, the first crash for a
         * boot may result in a 304 if the debuginfo had been downloaded in a
         * previous boot.
         */
        if (ret != 200 && ret != 404 && ret != 304) {
                urlcounter++;
        }

        if (ret == 200) {
                /* get timestamp, if any */
                curl_easy_getinfo(curl, CURLINFO_FILETIME, &changed);
                if (changed >= 0) {
                        struct timespec times[2];
                        times[0].tv_sec = (time_t)changed;
                        times[0].tv_nsec = 0;
                        times[1].tv_sec = (time_t)changed;
                        times[1].tv_nsec = 0;
                        futimens(fd, times);
                }

                autofree(char) *command = NULL;
                //		printf("Filename is %s\n", filename);

                memset(&statbuf, 0, sizeof(statbuf));
                stat(filename, &statbuf);
                if (statbuf.st_size > 0 &&
                    asprintf(&command,
                             "tar -C /var/cache/debuginfo/%s --no-same-owner "
                             "--no-same-permissions  -xf %s",
                             prefix,
                             filename) >= 0) {
                        if (system(command) != 0) {
                                fputs("Warning: tar extraction failed\n", stderr);
                        }
                }
        }
        unlink(filename);
        curl_easy_cleanup(curl);
        fclose(file);
        return ret;
}

double timedelta(struct timeval before, struct timeval after)
{
        double d;
        d = 1.0 * (after.tv_sec - before.tv_sec) +
            (1.0 * after.tv_usec - before.tv_usec) / 1000000.0;
        return d;
}

static void *server_thread(void *arg)
{
        int fd = -1;
        char buf[PATH_MAX + 8];
        int ret;
        char *prefix, *path, *c = NULL;
        autofree(char) *url = NULL;
        time_t timestamp;
        struct timeval before, after;
        __nc_unused__ size_t wr = -1;

        fd = (unsigned long)arg;
        if (fd < 0) {
                goto thread_end;
        }

        gettimeofday(&before, NULL);

        memset(buf, 0, sizeof(buf));
        ret = read(fd, buf, PATH_MAX + 8);
        if (ret < 0) {
                goto thread_end;
        }
        c = strchr(buf, ':');
        if (!c) {
                goto thread_end;
        }
        *c = 0;
        timestamp = strtoull(buf, NULL, 10);
        c++;
        prefix = c;
        path = strchr(c, ':');
        if (!path) {
                goto thread_end;
        }
        *path = 0;
        path++;

        /* GDB and elfutils both stat /usr/lib/debug directly when looking up
         * debuginfo, so avoid the download for "/.tar"; the associated cache
         * directories already exist by this point.
         */
        if (strlen(path) == 1 && strcmp(path, "/") == 0) {
                goto thread_end;
        }

        if (strstr(path, "..") || strstr(prefix, "..") || strstr(path, "'") || strstr(path, ";")) {
                goto thread_end;
        }

        if (strcmp(prefix, "lib") != 0 && strcmp(prefix, "src") != 0) {
                /* invalid prefix */
                goto thread_end;
        }
        url = NULL;
        if (asprintf(&url, "%s%s%s.tar", urls[urlcounter % 2], prefix, path) < 0) {
                goto thread_end;
        }

        //	printf("Getting url %s    %i:%06i\n", url, before.tv_sec, before.tv_usec);
        ret = curl_get_file(url, prefix, timestamp);

        switch (ret) {
        case 200:
        case 300:
        case 304:
        case 404:
                // ignore these error codes
                break;
        default:
                printf("Request for %s resulted in error %i\n", url, ret);
                break;
        }

        gettimeofday(&after, NULL);
#if 0        
        if (timedelta(before, after) > 0.6)
                printf("Request for %s took %5.2f seconds (%i - %i)\n",
                       url,
                       after.tv_sec - before.tv_sec +
                           (1.0 * after.tv_usec - before.tv_usec) / 1000000.0,
                       ret,
                       (int)timestamp);
#endif
        /* tell the other side we're done with the download */
        wr = write(fd, "ok", 3);

thread_end:
        if (fd >= 0) {
                close(fd);
        }
        dec_connection_count();
        return NULL;
}

int main(__nc_unused__ int argc, __nc_unused__ char **argv)
{
        int sockfd;
        struct sockaddr_un sun;
        int ret;
        int curl_done = 0;
        uid_t dbg_user = 0;
        gid_t dbg_group = 0;
        struct passwd *passwdentry;
        const char *required_paths[] = { "/var/cache/debuginfo/lib", "/var/cache/debuginfo/src" };

        umask(0);
        passwdentry = getpwnam("dbginfo");
        if (passwdentry) {
                dbg_user = passwdentry->pw_uid;
                dbg_group = passwdentry->pw_gid;
        }
        endpwent();

        if (prctl(PR_SET_DUMPABLE, 0) != 0) {
                fprintf(stderr,
                        "Failed to disable PR_SET_DUMPABLE. Do NOT gdb attach this process: %s\n",
                        strerror(errno));
        }

        if (geteuid() == 0) {
                if (prctl(PR_CAPBSET_DROP, CAP_SYS_ADMIN) != 0) {
                        fprintf(stderr, "Failed to drop caps: %s\n", strerror(errno));
                }
        }

        for (size_t i = 0; i < ARRAY_SIZE(required_paths); i++) {
                const char *req_path = required_paths[i];
                struct stat st = { .st_ino = 0 };
                if (lstat(req_path, &st) == 0) {
                        /* If the file already exists, check ownership
                         * and delete tree if incorrect. Essentially a
                         * one-off operation to transition from root owned to
                         * dbginfo owned */
                        if (st.st_uid == dbg_user) {
                                continue;
                        }
                        fprintf(stderr, "Removing old debug information %s\n", req_path);
                        nc_rm_rf(req_path);
                }
                if (!nc_mkdir_p(req_path, 00755)) {
                        fprintf(stderr, "Failed to mkdir: %s %s\n", strerror(errno), req_path);
                        return EXIT_FAILURE;
                }
                if (chown(req_path, dbg_user, dbg_group) != 0) {
                        fprintf(stderr, "Failed to chown: %s %s\n", strerror(errno), req_path);
                        return EXIT_FAILURE;
                }
        }

        signal(SIGPIPE, SIG_IGN);

        sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sockfd < 0) {
                return EXIT_FAILURE;
        }

        sun.sun_family = AF_UNIX;
        strcpy(sun.sun_path, ":clr-debug-info");
        sun.sun_path[0] = 0; /* anonymous unix socket */

        ret = bind(sockfd,
                   (struct sockaddr *)&sun,
                   offsetof(struct sockaddr_un, sun_path) + strlen(":clr-debug-info") + 1);
        if (ret < 0) {
                printf("Failed to bind:%s \n", strerror(errno));
                return EXIT_FAILURE;
        }

        if (listen(sockfd, 16) < 0) {
                printf("Failed to listen:%s \n", strerror(errno));
                return EXIT_FAILURE;
        }

        if (setgid(dbg_group)) {
                fprintf(stderr, "Unable to drop privileges setgid %s\n", strerror(errno));
                return EXIT_FAILURE;
        }
        if (setgroups(1, &dbg_group)) {
                fprintf(stderr, "Unable to drop privileges setgroups %s\n", strerror(errno));
                return EXIT_FAILURE;
        }
        if (setuid(dbg_user)) {
                fprintf(stderr, "Unable to drop privileges setuid  %s\n", strerror(errno));
                return EXIT_FAILURE;
        }

        while (1) {
                int clientsock;
                pthread_t thread;

                malloc_trim(0);
                clientsock = accept(sockfd, NULL, NULL);

                /* Too many connections, wait for the next loop/retry */
                if (get_current_connection_count() >= MAX_CONNECTIONS) {
                        /* printf("Too many connections!\n"); */
                        shutdown(clientsock, SHUT_RDWR);
                        close(clientsock);
                        continue;
                }

                if (!curl_done) {
                        curl_global_init(CURL_GLOBAL_ALL);
                        curl_done = 1;
                }

                /* Update the connection count and start the new thread */
                inc_connection_count();
                pthread_create(&thread, NULL, server_thread, (void *)(unsigned long)clientsock);
                pthread_detach(thread);
        }

        if (hash) {
                nc_hashmap_free(hash);
        }

        close(sockfd);
}
