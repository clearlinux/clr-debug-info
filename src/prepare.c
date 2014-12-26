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
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>

#include <glib.h>

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

	target = canonicalize_file_name(filename);
//	sleep(1);
//	printf("Symlink %s points to %s\n", filename, target);
	unlink(filename);
	link(target, filename);
}


static void wait_for_loadavg(void)
{
	FILE *file;
	char line[4096];
	while (1) {
		double d;
		file = fopen("/proc/loadavg", "r");
		if (!file)
			return;
		line[0] = 0;
		fgets(line, 4096, file);
		fclose(file);
		d = strtod(line, NULL);
		if (d < 50)
			return;
		sleep(5);
		
	}
}
int tarcount;

static void do_one_file(char *base1, char *base2, char *path, int isdir)
{
	char *fullpath1 = NULL, *fullpath2 = NULL, *dir, *dir2;
	struct stat buf1, buf2;
	int ret;

	if (asprintf(&fullpath1, "%s%s", base1, path) < 0)
		return;
	if (asprintf(&fullpath2, "%s%s.tar", base2, path) < 0) {
		free(fullpath1);
		return;
	}

	ret = lstat(fullpath1, &buf1);
	if (ret)
		goto out;

	if (S_ISLNK(buf1.st_mode)) {
//		printf("%s is a symlink\n", fullpath1);
		ret = stat(fullpath1, &buf2);
		if (!ret)
			unsymlink(fullpath1);
		ret = lstat(fullpath1, &buf1);		
	}	

	ret = lstat(fullpath2, &buf2);
	if (ret || buf1.st_mtime > buf2.st_mtime) {
		char *command = NULL;
		unlink(fullpath2);

		dir = strdup(fullpath2);

		dir2 = dirname(dir);

		if (asprintf(&command, "mkdir -p %s", dir2) >= 0) {
//			printf("Running -%s-\n", command);
			system(command);
			free(command);
		}

		free(dir);
		chdir(base1);
		if (!isdir && asprintf(&command, "tar --no-recursion -C %s -Jcf %s %s &", base1, fullpath2, path) >= 0) {
//			printf("Running -%s-\n", command);
			printf("Processing %s\n", fullpath2);
			tarcount++;
			if (tarcount > 50) {
				wait_for_loadavg();
				tarcount = 0;
			}
			system(command);
			free(command);
		}

		if (isdir && asprintf(&command, "tar --no-recursion -C %s -Jcf %s `find %s -type d` `find %s -maxdepth 1 -type l` &", base1, fullpath2, path, path) >= 0) {
//			printf("Running -%s-\n", command);
			printf("Processing %s\n", fullpath2);
			tarcount++;
			if (tarcount > 50) {
				wait_for_loadavg();
				tarcount = 0;
			}
			system(command);
			free(command);
		}
		
	}

out:
	free(fullpath1);
	free(fullpath2);	
}


static void recurse_dir(char *base1, char *base2, char *path)
{
	DIR *dir;
	char *fullpath1, *fullpath2, *newpath;
	struct dirent *entry;

	fullpath1 = NULL;
	fullpath2 = NULL;


	if (asprintf(&fullpath1, "%s%s", base1, path) < 0)
		return;

	dir = opendir(fullpath1);

	if (!dir) {
		free(fullpath1);
		return;
	}
	while (1) {
		entry = readdir(dir);
		if (!entry)
			break;
		if (strcmp(entry->d_name, ".") == 0)
			continue;
		if (strcmp(entry->d_name, "..") == 0)
			continue;
			
		newpath = NULL;
		fullpath2 = NULL;
		asprintf(&fullpath2, "%s/%s", fullpath1, entry->d_name);
		if (asprintf(&newpath, "%s/%s", path, entry->d_name) >= 0) {
			struct stat sb;
			stat(fullpath2, &sb);
			
			if (entry->d_type == DT_DIR || S_ISDIR(sb.st_mode)) {
				do_one_file(base1, base2, newpath, 1);
				recurse_dir(base1, base2, newpath);
			} else {
				do_one_file(base1, base2, newpath, 0);
			}		
			free(newpath);
		}
	}
	closedir(dir);
	free(fullpath1);
}

int main(int argc, char **argv)
{
	if (argc < 0)
		(void)argv;
	recurse_dir("/var/www/html/debuginfo.raw/usr/lib/debug/", "/var/www/html/debuginfo/lib/", ".");
	recurse_dir("/var/www/html/debuginfo.raw/usr/src/debug/", "/var/www/html/debuginfo/src/", ".");

	return EXIT_SUCCESS;
}
