/*
 *   Clear Linux -- automatic debug information installation
 *
 *      Copyright (C) 2013  Arjan van de Ven
 *     Curl portions borrowed from the Fenrus Update code
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
#include <libgen.h>
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
#include <fcntl.h>

#include "nica/files.h"
#include "nica/hashmap.h"

#include <curl/curl.h>

#include "systemd/sd-daemon.h"

#include "config.h"

#ifdef HAVE_ATOMIC_SUPPORT
#include <stdatomic.h>
#endif

#define TIMEOUT 600 /* 10 minutes */
#define DOWNLOAD_PARTS 2

typedef struct {
        double size;
        long too_old;
        long file_mtim;
        long ret;
} curl_info_t;

typedef struct {
        char *filename;
        CURL *handler;
        FILE *file;
} filepart_t;

static pthread_mutex_t dupes_mutex = PTHREAD_MUTEX_INITIALIZER;

char *urls_default[] = { "https://cdn.download.clearlinux.org/debuginfo/",
                         "https://cdn-alt.download.clearlinux.org/debuginfo/" };
int urls_size = 2;
int urlcounter = 1;
char **urls = urls_default;

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

/*
 * Read URLs from environment variable, space separated
 */
int configure_urls(void)
{
        const char *env_var = getenv("CLR_DEBUGINFO_URLS");
        const char *token = env_var;
        int count = 0;
        char **urls_new = NULL;
        if (env_var) {
                while (1) {
                        int token_len = strcspn(token, " \t\n");
                        if (token_len) {
                                count++;
                                urls_new = realloc(urls_new, count * sizeof(char *));
                                if (!urls_new) {
                                        perror("realloc()");
                                        exit(EXIT_FAILURE);
                                }

                                char *url = calloc(token_len + 1, sizeof(char));
                                if (!url) {
                                        perror("calloc()");
                                        exit(EXIT_FAILURE);
                                }

                                urls_new[count - 1] = strncpy(url, token, token_len);
                                token += token_len;
                        }
                        if (*token == '\0') {
                                break;
                        }
                        token++;
                }
        }
        if (count) {
                // Abandon defaults
                urls = urls_new;
                urls_size = count;
                urlcounter = 0;
        }
        return count;
}

void free_urls(void)
{
        if (urls != urls_default) {
                for (int i = 0; i < urls_size; i++) {
                        free(urls[i]);
                }
                free(urls);
        }
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

static size_t curl_header_handler(char *buffer, size_t size, size_t nitems, void *userdata)
{
        return nitems * size;
}

/**
 * Get http document header
 */
static int curl_get_file_info(const char *url, curl_info_t *info, time_t timestamp)
{
        CURL *curl_handler;
        CURLcode ret = 0;

        curl_handler = curl_easy_init();
        if (curl_handler == NULL)
                return -1;

        curl_easy_setopt(curl_handler, CURLOPT_URL, url);
        curl_easy_setopt(curl_handler, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl_handler, CURLOPT_HEADERFUNCTION, curl_header_handler);
        curl_easy_setopt(curl_handler, CURLOPT_FOLLOWLOCATION, 1L);

        /* request timestamp of files from server */
        curl_easy_setopt(curl_handler, CURLOPT_FILETIME, 1L);
        curl_easy_setopt(curl_handler, CURLOPT_TIMECONDITION, CURL_TIMECOND_IFMODSINCE);
        curl_easy_setopt(curl_handler, CURLOPT_TIMEVALUE, timestamp);

        do {
                if (curl_easy_perform(curl_handler) != CURLE_OK) {
                        ret = -1;
                        break;
                }
                if (curl_easy_getinfo(curl_handler, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &info->size) != CURLE_OK) {
                        ret = -1;
                        break;
                }
                if (curl_easy_getinfo(curl_handler, CURLINFO_CONDITION_UNMET, &info->too_old) != CURLE_OK) {
                        ret = -1;
                        break;
                }
                if (curl_easy_getinfo(curl_handler, CURLINFO_FILETIME, &info->file_mtim) != CURLE_OK) {
                        ret = -1;
                        break;
                }
                if (curl_easy_getinfo(curl_handler, CURLINFO_RESPONSE_CODE, &info->ret) != CURLE_OK) {
                        ret = -1;
                        break;
                }
        } while(false);
        curl_easy_cleanup(curl_handler);
        return ret;
}

static int curl_join_file(FILE* newfile, filepart_t *fileparts)
{
        int i;
        for (i = 0; i < DOWNLOAD_PARTS; i++) {
                long size;
                autofree(char) *buffer = NULL;
                fseek(fileparts[i].file, 0, SEEK_END);
                size = ftell(fileparts[i].file);
                fseek(fileparts[i].file, 0, SEEK_SET);

                buffer = malloc(size +1);
                fread(buffer, 1, size, fileparts[i].file);
                fwrite(buffer, 1, size, newfile);
        }
        return 0;
}

void close_n_unlink(filepart_t *filepart)
{
        curl_easy_cleanup(filepart->handler);
        fclose(filepart->file);
        unlink(filepart->filename);
        free(filepart->filename);
}

static int curl_download_file(const char *url, FILE *newfile, double size)
{
        double partSize = 0;
        double segLocation = 0;
        int still_running;
        const char *outputfile;
        filepart_t fileparts[DOWNLOAD_PARTS];
        CURLM *multi_handler;
        CURLMsg *msg;
        long counter = 0;
        int msgs_left;
        int i;

        partSize = size / DOWNLOAD_PARTS;

        outputfile = strrchr((const char*)url, '/') + 1;
        curl_global_init(CURL_GLOBAL_ALL);

        for (i = 0; i < DOWNLOAD_PARTS; i++) {
                autofree(char) *range = NULL;

                asprintf(&fileparts[i].filename, "/tmp/clr-debug-info-%s.part.%0d", outputfile, i);
                fileparts[i].handler = curl_easy_init();
                fileparts[i].file = fopen(fileparts[i].filename, "w+");
                setvbuf(fileparts[i].file, NULL, _IOLBF, 0);
                double nextPart = (i == DOWNLOAD_PARTS - 1)? size : segLocation + partSize - 1;

                asprintf(&range, "%0.0f-%0.0f", segLocation, nextPart);

                curl_easy_setopt(fileparts[i].handler, CURLOPT_URL, url);
                curl_easy_setopt(fileparts[i].handler, CURLOPT_RANGE, range);
                curl_easy_setopt(fileparts[i].handler, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(fileparts[i].handler, CURLOPT_NOPROGRESS, 1L);
                curl_easy_setopt(fileparts[i].handler, CURLOPT_WRITEDATA, fileparts[i].file);
                curl_easy_setopt(fileparts[i].handler, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);

                /*
                 * Some sane timeout values to prevent stalls
                 *
                 * Connect timeout is for first connection to the server.
                 * The low speed timeout is for slow downloads. Since many
                 * files are several mB large, we want to prevent them from
                 * taking forever. (1kB/sec avg over 30secs).
                 */
                curl_easy_setopt(fileparts[i].handler, CURLOPT_CONNECTTIMEOUT, 30);
                curl_easy_setopt(fileparts[i].handler, CURLOPT_LOW_SPEED_TIME, 30);
                curl_easy_setopt(fileparts[i].handler, CURLOPT_LOW_SPEED_LIMIT, 1024);

                segLocation = segLocation + partSize;
        }

        multi_handler = curl_multi_init();
        if (multi_handler == NULL) {
                return -1;
        }

        for (i = 0; i < DOWNLOAD_PARTS; i++) {
                curl_multi_add_handle(multi_handler, fileparts[i].handler);
        }

        curl_multi_perform(multi_handler, &still_running);

        do {
                struct timeval timeout;
                int rc;
                fd_set fdread;
                fd_set fdwrite;
                fd_set fdexcep;
                int max = -1;
                long curl_timeo = -1;

                FD_ZERO(&fdread);
                FD_ZERO(&fdwrite);
                FD_ZERO(&fdexcep);

                timeout.tv_sec = 100 * 1000;
                timeout.tv_usec = 0;

                curl_multi_timeout(multi_handler, &curl_timeo);
                if (curl_timeo >= 0) {
                        timeout.tv_sec = curl_timeo / 1000;
                        if (timeout.tv_sec > 1) {
                                timeout.tv_sec = 1;
                        } else {
                                timeout.tv_usec = (curl_timeo % 1000) * 1000;
                        }
                }

                curl_multi_fdset(multi_handler, &fdread, &fdwrite, &fdexcep, &max);

                rc = select(max + 1, &fdread, &fdwrite, &fdexcep, &timeout);

                switch (rc) {
                case -1:
                        fputs("Could not select the error\n", stderr);
                        break;
                case 0:
                default:
                        curl_multi_perform(multi_handler, &still_running);
                        break;
                }
        } while(still_running);

        while ((msg = curl_multi_info_read(multi_handler, &msgs_left))) {
                if (msg->msg == CURLMSG_DONE) {
                        int index, found = 0;
                        for (index = 0; index < DOWNLOAD_PARTS; index++) {
                                found = (msg->easy_handle == fileparts[index].handler);
                                if (found) {
                                        counter++;
                                        break;
                                }
                        }
                }
        }

        curl_multi_cleanup(multi_handler);

        curl_join_file(newfile, &fileparts[0]);

        for (i = 0; i < DOWNLOAD_PARTS; i++) {
                close_n_unlink(&fileparts[i]);
        }
        return counter != DOWNLOAD_PARTS;
}

static int curl_get_file(const char *url, const char *prefix, time_t timestamp)
{
        curl_info_t info;
        long ret;
        int fd;
        autofree(char) *filename = NULL;
        FILE *file;

        if (avoid_dupes(url)) {
                return 300;
        }

        if (curl_get_file_info(url, &info, timestamp)) {
                return 418;
        }
        ret = info.ret;

        if (info.ret != 200 && info.ret != 404 && info.ret != 304) {
                urlcounter++;
        }

        if (asprintf(&filename, "/tmp/clr-debug-info-XXXXXX") < 0) {
                ret = 418;
        }

        fd = mkstemp(filename);
        if (fd < 0) {
                ret = 418;
        }

        file = fdopen(fd, "w");
        if (curl_download_file(url, file, info.size)) {
                ret = 418;
        }

        fflush(file);

        if (info.ret == 200) {
                autofree(char) *command = NULL;
                struct stat statbuf;

                /* get timestamp, if any */
                if (!info.too_old) {
                        struct timespec times[2];
                        times[0].tv_sec = (time_t)info.file_mtim;
                        times[0].tv_nsec = 0;
                        times[1].tv_sec = (time_t)info.file_mtim;
                        times[1].tv_nsec = 0;
                        futimens(fd, times);
                }

                memset(&statbuf, 0, sizeof(statbuf));
                stat(filename, &statbuf);
                if (statbuf.st_size <= 0) {
                        ret = 418;
                        goto out;
                }

                /* test extraction first */
                if (asprintf(&command,
                             "tar -C /var/cache/debuginfo/%s --no-same-owner "
                             "--no-same-permissions -tf %s",
                             prefix,
                             filename) < 0) {
                        ret = 418;
                        goto out;
                }

                if (system(command) != 0) {
                        ret = 418;
                        goto out;
                }

                free(command); /* reuse */
                if (asprintf(&command,
                             "tar -C /var/cache/debuginfo/%s --no-same-owner "
                             "--no-same-permissions -xf %s",
                             prefix,
                             filename) < 0) {
                        ret = 418;
                        goto out;
                }

                if (system(command) != 0) {
                        ret = 418;
                        fprintf(stderr, "Error: tar extraction failed\n");
                        goto out;
                }
        }

out:
        unlink(filename);
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
        if (asprintf(&url, "%s%s%s.tar", urls[urlcounter % urls_size], prefix, path) < 0) {
                goto thread_end;
        }

        //        printf("Getting url %s    %i:%06i\n", url, before.tv_sec, before.tv_usec);
        ret = curl_get_file(url, prefix, timestamp);

        switch (ret) {
        case 200:
        case 300:
        case 304:
        case 404:
                // ignore these error codes
                break;
        default:
                fprintf(stderr, "Request for %s resulted in error %i\n", url, ret);
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

        if (configure_urls()) {
                fprintf(stderr, "Using urls from environment\n");
        } else {
                fprintf(stderr, "Using compiled default urls\n");
        }
        for (int i = 0; i < urls_size; i++) {
                fprintf(stderr, "url: %s\n", urls[i]);
        }

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

        if (sd_listen_fds(0) == 1) {
                /* systemd socket activation */
                sockfd = SD_LISTEN_FDS_START + 0;
        } else if (sd_listen_fds(0) > 1) {
                fprintf(stderr, "Too many file descriptors received.\n");
                exit(EXIT_FAILURE);
        } else {
                sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
                if (sockfd < 0) {
                        exit(EXIT_FAILURE);
                }

                sun.sun_family = AF_UNIX;
                strcpy(sun.sun_path, SOCKET_PATH);

                ret = bind(sockfd,
                           (struct sockaddr *)&sun,
                           offsetof(struct sockaddr_un, sun_path) + strlen(SOCKET_PATH) + 1);
                if (ret < 0) {
                        fprintf(stderr, "Failed to bind:%s \n", strerror(errno));
                        exit(EXIT_FAILURE);
                }

                if (listen(sockfd, 16) < 0) {
                        fprintf(stderr, "Failed to listen:%s \n", strerror(errno));
                        exit(EXIT_FAILURE);
                }
        }

        if (setgid(dbg_group)) {
                fprintf(stderr, "Unable to drop privileges setgid %s\n", strerror(errno));
                exit(EXIT_FAILURE);
        }
        if (setgroups(1, &dbg_group)) {
                fprintf(stderr, "Unable to drop privileges setgroups %s\n", strerror(errno));
                exit(EXIT_FAILURE);
        }
        if (setuid(dbg_user)) {
                fprintf(stderr, "Unable to drop privileges setuid  %s\n", strerror(errno));
                exit(EXIT_FAILURE);
        }

        while (1) {
                fd_set rfds;
                struct timeval tv;
                int ret;
                int clientsock;
                pthread_t thread;

                malloc_trim(0);

                /* use select() to timeout and exit gracefully */
                FD_ZERO(&rfds);
                FD_SET(sockfd, &rfds);
                tv.tv_sec = TIMEOUT;
                tv.tv_usec = 0;
                ret = select(sockfd + 1, &rfds, NULL, NULL, &tv);
                if (ret == -1) {
                        perror("select()");
                        exit(EXIT_FAILURE);
                } else if (ret == 0) {
                        break;
                }

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

        free_urls();
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 expandtab:
 * :indentSize=8:tabSize=8:noTabs=true:
 */
