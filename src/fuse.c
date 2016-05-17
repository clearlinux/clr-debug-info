/*


This is a lightly modified version of the fuse example "fusexmp.c",
the original copyright notice is retained below.
The modifications to the example are
        (C) 2013 Arjan van de Ven <arjanvandeven@gmail.com>
            Ikey Doherty <michael.i.doherty@intel.com>



Original copyright notice follows:

  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall fusexmp.c `pkg-config fuse --cflags --libs` -o fusexmp
*/

#define _GNU_SOURCE
#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <malloc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include "nica/files.h"
#include "nica/util.h"

extern void try_to_get(const char *path, int pid, time_t timestamp);

__attribute__((always_inline)) static inline char *xmp_make_dotpath(const char *path)
{
        char *newp = NULL;
        if (asprintf(&newp, ".%s", path) < 0) {
                return NULL;
        }
        return newp;
}

static int xmp_getattr(const char *path, struct stat *stbuf)
{
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        memset(stbuf, 0, sizeof(struct stat));
        res = lstat(newpath, stbuf);

        /*
         * get the file. if the st_mtime is set, this is just an async refresh, otherwise it's
         * a synchronous request.
         */
        try_to_get(path, fuse_get_context()->pid, stbuf->st_mtime);

        res = lstat(newpath, stbuf);

        if (res == -1) {
                return -errno;
        }

        return 0;
}

static int xmp_access(const char *path, int mask)
{
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        res = access(newpath, mask);
        if (res == -1) {
                return -errno;
        }

        return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        res = readlink(newpath, buf, size - 1);
        if (res == -1) {
                return -errno;
        }

        buf[res] = '\0';
        return 0;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                       struct fuse_file_info *fi)
{
        DIR *dp;
        struct dirent *de;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        (void)offset;
        (void)fi;

        dp = opendir(newpath);
        if (dp == NULL) {
                return -errno;
        }

        while ((de = readdir(dp)) != NULL) {
                struct stat st;
                memset(&st, 0, sizeof(st));
                st.st_ino = de->d_ino;
                st.st_mode = de->d_type << 12;
                if (filler(buf, de->d_name, &st, 0)) {
                        break;
                }
        }

        closedir(dp);
        return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        /* On Linux this could just be 'mknod(path, mode, rdev)' but this
           is more portable */
        if (S_ISREG(mode)) {
                res = open(newpath, O_CREAT | O_EXCL | O_WRONLY, mode);
                if (res >= 0) {
                        res = close(res);
                }
        } else if (S_ISFIFO(mode)) {
                res = mkfifo(newpath, mode);
        } else {
                res = mknod(newpath, mode, rdev);
        }
        if (res == -1) {
                return -errno;
        }

        return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        res = mkdir(newpath, mode);
        if (res == -1) {
                return -errno;
        }

        return 0;
}

static int xmp_unlink(const char *path)
{
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        res = unlink(newpath);
        if (res == -1) {
                return -errno;
        }

        return 0;
}

static int xmp_rmdir(const char *path)
{
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        res = rmdir(newpath);
        if (res == -1) {
                return -errno;
        }

        return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
        int res;
        autofree(char) *newfrom = NULL;
        autofree(char) *newto = NULL;

        if ((newfrom = xmp_make_dotpath(from)) == NULL) {
                return -ENOMEM;
        }
        if ((newto = xmp_make_dotpath(to)) == NULL) {
                return -ENOMEM;
        }

        res = symlink(newfrom, newto);
        if (res == -1) {
                return -errno;
        }

        return 0;
}

static int xmp_rename(const char *from, const char *to)
{
        int res;
        autofree(char) *newfrom = NULL;
        autofree(char) *newto = NULL;

        if ((newfrom = xmp_make_dotpath(from)) == NULL) {
                return -ENOMEM;
        }
        if ((newto = xmp_make_dotpath(to)) == NULL) {
                return -ENOMEM;
        }

        res = rename(newfrom, newto);
        if (res == -1) {
                return -errno;
        }

        return 0;
}

static int xmp_link(const char *from, const char *to)
{
        int res;
        autofree(char) *newfrom = NULL;
        autofree(char) *newto = NULL;

        if ((newfrom = xmp_make_dotpath(from)) == NULL) {
                return -ENOMEM;
        }
        if ((newto = xmp_make_dotpath(to)) == NULL) {
                return -ENOMEM;
        }

        res = link(newfrom, newto);

        if (res == -1) {
                return -errno;
        }

        return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        res = chmod(newpath, mode);
        if (res == -1) {
                return -errno;
        }

        return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        res = lchown(newpath, uid, gid);
        if (res == -1) {
                return -errno;
        }

        return 0;
}

static int xmp_truncate(const char *path, off_t size)
{
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        res = truncate(newpath, size);
        if (res == -1) {
                return -errno;
        }

        return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2])
{
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        /* don't use utime/utimes since they follow symlinks */
        res = utimensat(0, newpath, ts, AT_SYMLINK_NOFOLLOW);
        if (res == -1) {
                return -errno;
        }

        return 0;
}
#endif

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        res = open(newpath, fi->flags);
        if (res == -1) {
                return -errno;
        }

        close(res);
        return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
        int fd;
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        (void)fi;
        fd = open(newpath, O_RDONLY);
        if (fd == -1) {
                return -errno;
        }

        res = pread(fd, buf, size, offset);
        if (res == -1) {
                res = -errno;
        }

        close(fd);
        return res;
}

static int xmp_write(const char *path, const char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
        int fd;
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        (void)fi;
        fd = open(newpath, O_WRONLY);
        if (fd == -1) {
                return -errno;
        }

        res = pwrite(fd, buf, size, offset);
        if (res == -1) {
                res = -errno;
        }

        close(fd);
        return res;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
        int res;
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        res = statvfs(newpath, stbuf);
        if (res == -1) {
                return -errno;
        }

        return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
        /* Just a stub.	 This method is optional and can safely be left
           unimplemented */

        (void)path;
        (void)fi;
        return 0;
}

static int xmp_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
        /* Just a stub.	 This method is optional and can safely be left
           unimplemented */

        (void)path;
        (void)isdatasync;
        (void)fi;
        return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int xmp_fallocate(const char *path, int mode, off_t offset, off_t length,
                         struct fuse_file_info *fi)
{
        int fd;
        int res;
        autofree(char) *newpath = NULL;

        (void)fi;

        if (mode) {
                return -EOPNOTSUPP;
        }

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        fd = open(newpath, O_WRONLY);
        if (fd == -1) {
                return -errno;
        }

        res = -posix_fallocate(fd, offset, length);

        close(fd);
        return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value, size_t size,
                        int flags)
{
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        int res = lsetxattr(newpath, name, value, size, flags);
        if (res == -1) {
                return -errno;
        }
        return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value, size_t size)
{
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        int res = lgetxattr(newpath, name, value, size);
        if (res == -1) {
                return -errno;
        }
        return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        int res = llistxattr(newpath, list, size);
        if (res == -1) {
                return -errno;
        }
        return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
        autofree(char) *newpath = NULL;

        if ((newpath = xmp_make_dotpath(path)) == NULL) {
                return -ENOMEM;
        }

        int res = lremovexattr(newpath, name);
        if (res == -1) {
                return -errno;
        }
        return 0;
}
#endif /* HAVE_SETXATTR */

int save_dir;

static void *xmp_init(__nc_unused__ struct fuse_conn_info *conn)
{
        __nc_unused__ int r = fchdir(save_dir);
        close(save_dir);

        return NULL;
}

static struct fuse_operations xmp_oper = {
        .init = xmp_init,
        .getattr = xmp_getattr,
        .access = xmp_access,
        .readlink = xmp_readlink,
        .readdir = xmp_readdir,
        .mknod = xmp_mknod,
        .mkdir = xmp_mkdir,
        .symlink = xmp_symlink,
        .unlink = xmp_unlink,
        .rmdir = xmp_rmdir,
        .rename = xmp_rename,
        .link = xmp_link,
        .chmod = xmp_chmod,
        .chown = xmp_chown,
        .truncate = xmp_truncate,
#ifdef HAVE_UTIMENSAT
        .utimens = xmp_utimens,
#endif
        .open = xmp_open,
        .read = xmp_read,
        .write = xmp_write,
        .statfs = xmp_statfs,
        .release = xmp_release,
        .fsync = xmp_fsync,
#ifdef HAVE_POSIX_FALLOCATE
        .fallocate = xmp_fallocate,
#endif
#ifdef HAVE_SETXATTR
        .setxattr = xmp_setxattr,
        .getxattr = xmp_getxattr,
        .listxattr = xmp_listxattr,
        .removexattr = xmp_removexattr,
#endif
};

extern char *prefix;

int main(__nc_unused__ int argc, __nc_unused__ char *argv[])
{
        char *fake_argv[20];
        char *dir = "/usr/src/debug";
        char *shadowdir = "/var/cache/debuginfo/src";
        const char *required_paths[] = { "/var/cache/debuginfo/lib", "/var/cache/debuginfo/src" };

        umask(0);

        /* give the system some time to boot before we go active; this is a background task */
        sleep(1);

        if (access("/sys/module/fuse/", F_OK)) {
                /* Failure would happen later in fuse_main, reduce double checks. */
                __nc_unused__ int ret = system("modprobe fuse");
        }
        signal(SIGPIPE, SIG_IGN);

        for (size_t i = 0; i < ARRAY_SIZE(required_paths); i++) {
                const char *req_path = required_paths[i];
                if (nc_file_exists(req_path)) {
                        continue;
                }
                if (!nc_mkdir_p(req_path, 00755)) {
                        fprintf(stderr, "Failed to mkdir: %s %s\n", strerror(errno), req_path);
                        return EXIT_FAILURE;
                }
        }

        if (fork() == 0) {
                dir = "/usr/lib/debug";
                shadowdir = "/var/cache/debuginfo/lib";
                prefix = "lib";
        }

        save_dir = open(shadowdir, O_RDONLY);

        fake_argv[0] = "clr_debug_fuse";
        fake_argv[1] = "-f";
        fake_argv[2] = "-o";
        fake_argv[3] = "nonempty";
        fake_argv[4] = "-o";
        fake_argv[5] = "allow_other";
        fake_argv[6] = "-o";
        fake_argv[7] = "default_permissions";
        fake_argv[8] = dir;

        return fuse_main(9, fake_argv, &xmp_oper, NULL);
}
