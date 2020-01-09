#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include "config.h"

#define BUFFER_LENGTH 16

int get_server_socket(void)
{
        int sockfd;
        struct sockaddr_un serveraddr;
        fputs("Creating socket\n", stderr);
        sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sockfd < 0) {
                fputs("Fail creating socket\n", stderr);
                return -1;
        }
        memset(&serveraddr, 0, sizeof(serveraddr));
        serveraddr.sun_family = AF_UNIX;
        strcpy(serveraddr.sun_path, SOCKET_PATH);

        fputs("Connecting to server\n", stderr);
        if (connect(sockfd, (struct sockaddr*)&serveraddr,
                    offsetof(struct sockaddr_un, sun_path) + strlen(SOCKET_PATH) + 1) < 0) {
                fprintf(stderr, "Fail connecting to server: %s\n", strerror(errno));
                close(sockfd);
                return -1;
        }
        return sockfd;
}


int testing_daemon(void)
{
        int sockfd;
        char buffer[BUFFER_LENGTH];

        if((sockfd = get_server_socket()) < 0) {
                return -1;
        }

        fputs("Sending message to server\n", stderr);
        memset(buffer, 0, sizeof(buffer));
        strcpy(buffer, "0:lib:/lib");
        if(send(sockfd, buffer, sizeof(buffer), 0) < 0) {
                fputs("Fail sending message to socket service\n", stderr);
                close(sockfd);
                return -1;
        }

        fputs("Receiving message from server\n", stderr);
        memset(buffer, 0, sizeof(buffer));
        if (recv(sockfd, &buffer[0],
                 BUFFER_LENGTH, 0) < 0) {
                fputs("Failing receiving message from socket service\n", stderr);
                close(sockfd);
                return -1;
        }

        close(sockfd);
        return strcmp(buffer, "ok") && access("/var/cache/debuginfo/lib/lib", F_OK) != -1;
}

int main(void)
{
        int res = 0;
        pid_t p;
        p = fork();
        if (p == 0) {
                if (freopen("/dev/null", "w", stderr) == NULL)
                        return -1;
                char *args[] = {"./clr_debug_daemon", NULL};
                execvp("./clr_debug_daemon", args);
        } else if (p > 0) {
                sleep(1);
                res = testing_daemon();
                kill(p, SIGKILL);
        } else {
                fputs("error executing fork\n", stderr);
        }
        unlink(SOCKET_PATH);
        return res;
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
