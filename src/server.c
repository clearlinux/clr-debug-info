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
 *
 */

#define _GNU_SOURCE

#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "nica/hashmap.h"
#include <curl/curl.h>

static pthread_mutex_t dupes_mutex = PTHREAD_MUTEX_INITIALIZER;

char *urls[2] = { "https://debuginfo.clearlinux.org/debuginfo/",
                  "https://debuginfo.clearlinux.org/debuginfo/" };
int urlcounter = 1;

static NcHashmap *hash = NULL;

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

static int curl_get_file(const char *url, const char *prefix, time_t timestamp)
{
        CURLcode code;
        long ret;
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
                char *command = NULL;
                //		printf("Filename is %s\n", filename);

                memset(&statbuf, 0, sizeof(statbuf));
                stat(filename, &statbuf);
                if (statbuf.st_size > 0 &&
                    asprintf(&command,
                             "tar -C /var/cache/debuginfo/%s --no-same-owner  -xf %s",
                             prefix,
                             filename) >= 0) {
                        system(command);
                        free(command);
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
        int fd;
        char buf[PATH_MAX + 8];
        int ret;
        char *prefix, *path, *url, *c;
        time_t timestamp;
        struct timeval before, after;

        fd = (unsigned long)arg;
        if (fd < 0) {
                return NULL;
        }

        gettimeofday(&before, NULL);

        memset(buf, 0, sizeof(buf));
        ret = read(fd, buf, PATH_MAX + 8);
        if (ret < 0) {
                close(fd);
                return NULL;
        }
        prefix = buf;
        c = strchr(buf, ':');
        if (!c) {
                close(fd);
                return NULL;
        }
        *c = 0;
        timestamp = strtoull(buf, NULL, 10);
        c++;
        prefix = c;
        path = strchr(c, ':');
        if (!path) {
                close(fd);
                return NULL;
        }
        *path = 0;
        path++;

        /* GDB and elfutils both stat /usr/lib/debug directly when looking up
         * debuginfo, so avoid the download for "/.tar"; the associated cache
         * directories already exist by this point.
         */
        if (strlen(path) == 1 && strcmp(path, "/") == 0) {
                close(fd);
                return NULL;
        }

        if (strstr(path, "..") || strstr(prefix, "..") || strstr(path, "'") || strstr(path, ";")) {
                close(fd);
                return NULL;
        }

        if (strcmp(prefix, "lib") != 0 && strcmp(prefix, "src") != 0) {
                /* invalid prefix */
                close(fd);
                return NULL;
        }
        url = NULL;
        if (asprintf(&url, "%s%s%s.tar", urls[urlcounter % 2], prefix, path) < 0) {
                close(fd);
                return NULL;
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
        if (timedelta(before, after) > 0.6)
                printf("Request for %s took %5.2f seconds (%i - %i)\n",
                       url,
                       after.tv_sec - before.tv_sec +
                           (1.0 * after.tv_usec - before.tv_usec) / 1000000.0,
                       ret,
                       (int)timestamp);

        /* tell the other side we're done with the download */
        write(fd, "ok", 3);
        close(fd);

        free(url);
        return NULL;
}

int main(__nc_unused__ int argc, __nc_unused__ char **argv)
{
        int sockfd;
        struct sockaddr_un sun;
        int ret;
        int curl_done = 0;

        signal(SIGPIPE, SIG_IGN);

        if (access("/var/cache/debuginfo/lib/", F_OK)) {
                system("mkdir -p /var/cache/debuginfo/lib/ &> /dev/null");
        }
        if (access("/var/cache/debuginfo/src/", F_OK)) {
                system("mkdir -p /var/cache/debuginfo/src/ &> /dev/null");
        }

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

        while (1) {
                int clientsock;
                pthread_t thread;

                malloc_trim(0);
                clientsock = accept(sockfd, NULL, NULL);

                if (!curl_done) {
                        curl_global_init(CURL_GLOBAL_ALL);
                        curl_done = 1;
                }

                pthread_create(&thread, NULL, server_thread, (void *)(unsigned long)clientsock);
                pthread_detach(thread);
        }

        if (hash) {
                nc_hashmap_free(hash);
        }

        close(sockfd);
}
