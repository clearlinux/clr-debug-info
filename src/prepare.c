/*
 *   Fenrus Linux -- automatic debug information installation
 *   Clear Linux -- automatic debug information installation
 *   Server side preparation logic
 *
 *      Copyright (C) 2013  Arjan van de Ven
 *      Copyright (C) 2014  Intel Corporation
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
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "nica/files.h"

/*

this program takes a directory /var/www/html/debuginfo.raw,
in which all debuginfo rpms have previously been rpm2cpio'd, and
creates the content for /var/www/html/debuginfo
which contains tar'd up/compressed versions of all the files.
As part of this symlinks will be resolved server side
as much as possible.

*/

static void unsymlink(char *filename)
{
        char *target;
        __nc_unused__ int r;

        target = canonicalize_file_name(filename);
        //	sleep(1);
        //	printf("Symlink %s points to %s\n", filename, target);
        unlink(filename);
        r = link(target, filename);
}

static void wait_for_loadavg(void)
{
        FILE *file;
        char line[4096];
        __nc_unused__ char *ret = NULL;

        while (1) {
                double d;
                file = fopen("/proc/loadavg", "r");
                if (!file) {
                        return;
                }
                line[0] = 0;
                ret = fgets(line, 4096, file);
                fclose(file);
                d = strtod(line, NULL);
                if (d < 50) {
                        return;
                }
                sleep(5);
        }
}
int tarcount;

static void do_one_file(char *base1, char *base2, char *path, int isdir)
{
        char *dir2 = NULL;
        autofree(char) * fullpath1, *fullpath2, *dir = NULL;
        struct stat buf1, buf2;
        int ret;
        int sret;

        if (asprintf(&fullpath1, "%s%s", base1, path) < 0) {
                return;
        }
        if (asprintf(&fullpath2, "%s%s.tar", base2, path) < 0) {
                return;
        }

        ret = lstat(fullpath1, &buf1);
        if (ret) {
                return;
        }

        if (S_ISLNK(buf1.st_mode)) {
                //		printf("%s is a symlink\n", fullpath1);
                ret = stat(fullpath1, &buf2);
                if (!ret) {
                        unsymlink(fullpath1);
                }
                ret = lstat(fullpath1, &buf1);
        }

        ret = lstat(fullpath2, &buf2);
        if (ret || buf1.st_mtime > buf2.st_mtime) {
                char *command = NULL;
                unlink(fullpath2);

                dir = strdup(fullpath2);

                dir2 = dirname(dir);

                if (dir2) {
                        (void)nc_mkdir_p(dir2, 00755);
                }

                if (chdir(base1) != 0) {
                        fprintf(stderr, "Failed to chdir: %s\n", strerror(errno));
                        return;
                }

                if (!isdir &&
                    asprintf(&command,
                             "tar --no-recursion -C %s -Jcf %s %s &",
                             base1,
                             fullpath2,
                             path) >= 0) {
                        //			printf("Running -%s-\n", command);
                        printf("Processing %s\n", fullpath2);
                        tarcount++;
                        if (tarcount > 50) {
                                wait_for_loadavg();
                                tarcount = 0;
                        }
                        if ((sret = system(command)) != 0) {
                                fprintf(stderr, "Failure in command: [%d] %s\n", sret, command);
                        }
                        free(command);
                }

                if (isdir &&
                    asprintf(&command,
                             "tar --no-recursion -C %s -Jcf %s `find %s -type d` `find %s "
                             "-maxdepth 1 -type l` &",
                             base1,
                             fullpath2,
                             path,
                             path) >= 0) {
                        //			printf("Running -%s-\n", command);
                        printf("Processing %s\n", fullpath2);
                        tarcount++;
                        if (tarcount > 50) {
                                wait_for_loadavg();
                                tarcount = 0;
                        }
                        if ((sret = system(command)) != 0) {
                                fprintf(stderr, "Failure in command: [%d] %s\n", sret, command);
                        }
                        free(command);
                }
        }
}

static void recurse_dir(char *base1, char *base2, char *path)
{
        DIR *dir;
        char *fullpath1 = NULL;
        struct dirent *entry;

        if (asprintf(&fullpath1, "%s%s", base1, path) < 0) {
                return;
        }

        dir = opendir(fullpath1);

        if (!dir) {
                return;
        }

        while (1) {
                autofree(char) *fullpath2 = NULL;
                autofree(char) *newpath = NULL;

                entry = readdir(dir);
                if (!entry) {
                        break;
                }
                if (strcmp(entry->d_name, ".") == 0) {
                        continue;
                }
                if (strcmp(entry->d_name, "..") == 0) {
                        continue;
                }

                newpath = NULL;
                fullpath2 = NULL;
                if (asprintf(&fullpath2, "%s/%s", fullpath1, entry->d_name) < 0) {
                        return;
                }
                if (asprintf(&newpath, "%s/%s", path, entry->d_name) >= 0) {
                        struct stat sb;
                        stat(fullpath2, &sb);

                        if (entry->d_type == DT_DIR || S_ISDIR(sb.st_mode)) {
                                do_one_file(base1, base2, newpath, 1);
                                recurse_dir(base1, base2, newpath);
                        } else {
                                do_one_file(base1, base2, newpath, 0);
                        }
                }
        }
        closedir(dir);
}

int main(int argc, char **argv)
{
        if (argc < 0)
                (void)argv;
        recurse_dir("/var/www/html/debuginfo.raw/usr/lib/debug/",
                    "/var/www/html/debuginfo/lib/",
                    ".");
        recurse_dir("/var/www/html/debuginfo.raw/usr/src/debug/",
                    "/var/www/html/debuginfo/src/",
                    ".");

        return EXIT_SUCCESS;
}
