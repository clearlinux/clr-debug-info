#define _GNU_SOURCE

#include <errno.h>
#include <grp.h>
#include <linux/capability.h>
#include <linux/limits.h>
#include <malloc.h>
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
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include "config.h"

#define TIMEOUT 600
#define MAX_CONNECTIONS 16

int check_file_request(int fd, char *expected_str)
{
        char buf[PATH_MAX + 8];
        int clientsock;
        malloc_trim(0);
        fputs("Accepting connection\n", stderr);
        clientsock = accept(fd, NULL, NULL);
        if (clientsock < 0)
                return -1;

        memset(buf, 0, sizeof(buf));
        fputs("Reading socket content\n", stderr);
        if (read(clientsock, buf, PATH_MAX + 8) < 0)
                return -1;
        close(clientsock);
        return strncmp(buf, expected_str, strlen(expected_str));
}

int get_service_socket(void)
{
        int sockfd;
        struct sockaddr_un sun;
        signal(SIGPIPE, SIG_IGN);
        fputs("Creating socket\n", stderr);
        sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sockfd < 0) {
                fputs("Fail creating socket\n", stderr);
                return -1;
        }

        printf("Socket path %s\n", SOCKET_PATH);
        sun.sun_family = AF_UNIX;
        strcpy(sun.sun_path, SOCKET_PATH);

        fputs("Binding socket\n", stderr);
        if (bind(sockfd, (struct sockaddr*)&sun,
                   offsetof(struct sockaddr_un, sun_path) + strlen(SOCKET_PATH) + 1) < 0) {
                fprintf(stderr, "fail bind: %s\n", strerror(errno));
                close(sockfd);
                return -1;
        }

        if (listen(sockfd, 16) < 0) {
                fputs("Fail listening socket \n", stderr);
                close(sockfd);
                return -1;
        }
        return sockfd;
}


int testing_fuse(void)
{
        int sockfd;
        uid_t gdb_user = 0;
        gid_t gdb_group = 0;
        struct passwd *passwdentry;
        pid_t p2;
        int test_result = -1;
        umask(0);
        passwdentry =  getpwnam("gdbinfo");
        if (passwdentry) {
                gdb_user = passwdentry->pw_uid;
                gdb_group = passwdentry->pw_gid;
        }
        endpwent();

        if (prctl(PR_SET_DUMPABLE, 0) != 0) {
                fputs("fail prctl\n", stderr);
        }

        if (geteuid() == 0) {
                if (prctl(PR_CAPBSET_DROP, CAP_SYS_ADMIN) !=0) {
                        fprintf(stderr, "fail geteuid: %s\n", strerror(errno));
                }
        }

        if ((sockfd = get_service_socket()) < 0) {
                return 1;
        }

        if (setgid(gdb_group)) {
                fputs("fail setgid\n", stderr);
        }

        if (setgroups(1, &gdb_group)) {
                fputs("fail setgroups\n", stderr);
        }

        if (setuid(gdb_user)) {
                fputs("fail setuid\n", stderr);
        }

        p2 = fork();
        if (p2 > 0) {
                test_result = check_file_request(sockfd, "0:lib:/tmp");
        } else if (p2 == 0) {
                if (freopen("/dev/null", "w", stderr) == NULL)
                        exit(1);
                char *args[] = {"/usr/bin/cat", "/usr/lib/debug/tmp", NULL};
                execvp("/usr/bin/cat", args);
        } else {
                fputs("error executing fork\n", stderr);
                return 1;
        }

        wait(NULL);
        close(sockfd);
        return test_result;
}

int main(void)
{
        int res = 0;
        pid_t p;
        p = fork();
        if (p == 0) {
                if (freopen("/dev/null", "w", stderr) == NULL)
                        return -1;
                char *args[] = {"./clr_debug_fuse", NULL};
                execvp("./clr_debug_fuse", args);
        } else if (p > 0) {
                res = testing_fuse();
                kill(p, SIGKILL);
        } else {
                fputs("error executing fork\n", stderr);
                return 1;
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
