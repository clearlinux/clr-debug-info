/*
 *   Clear Linux -- automatic debug information installation
 *
 *      Copyright (C) 2013  Arjan van de Ven
 *	Copyright (C) 2014  Intel Corporation
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, version 2 or later of the License.
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

#include "nica/util.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

/* 0.75 seconds timeout */
#define TIMEOUT 75000
#define TIMEOUT2 1500
#define TIMEOUT3 500

char *prefix = "src";

time_t deadtime;

void try_to_get(const char *path, int pid, time_t timestamp)
{
        int sockfd;
        struct sockaddr_un sun;
        int ret;
        socklen_t len;
        char *command;
        fd_set rfds;
        struct timeval tv;
        struct ucred cred;
        int shorttime = 0;
        __nc_unused__ size_t wr = -1;

        // printf("Trying to aquire %s\n", path);

        sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sockfd < 0) {
                return;
        }

        sun.sun_family = AF_UNIX;
        strcpy(sun.sun_path, SOCKET_PATH);

        ret = connect(sockfd,
                      (struct sockaddr *)&sun,
                      offsetof(struct sockaddr_un, sun_path) + strlen(SOCKET_PATH) + 1);
        if (ret < 0) {
                printf("Cannot connect to %s: %s\n", SOCKET_PATH, strerror(errno));
                close(sockfd);
                return;
        }

        len = sizeof(cred);
        memset(&cred, 0, sizeof(cred));
        ret = getsockopt(sockfd, SOL_SOCKET, SO_PEERCRED, &cred, &len);

        if (ret == 0 && cred.pid == pid) {
                printf("Recursion\n");
                close(sockfd);
                return;
        }

        command = NULL;
        if (asprintf(&command, "%llu:%s:%s", (unsigned long long)timestamp, prefix, path) < 0) {
                close(sockfd);
                return;
        }
        wr = write(sockfd, command, strlen(command) + 1);

        FD_ZERO(&rfds);
        FD_SET(sockfd, &rfds);

        /* Wait up to 0.5 seconds. */
        tv.tv_sec = 0;
        tv.tv_usec = TIMEOUT;

        /* if we had a timeout recently, must go quicker to avoid sequential delays */
        if (deadtime > time(NULL)) {
                tv.tv_usec = TIMEOUT2;
                shorttime = 1;
        }

        ret = 1;

        if (!timestamp) {
                ret = select(sockfd + 1, &rfds, NULL, &rfds, &tv);
        }
        if (ret == 0 && !shorttime) {
                printf("timeout for %s\n", path);
                deadtime = time(NULL) + 4;
        }

        close(sockfd);
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
