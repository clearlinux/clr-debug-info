/*


This is a lightly modified version of the fuse example "fusexmp.c",
the original copyright notice is retained below.
The modifications to the example are 
	(C) 2013 Arjan van de Ven <arjanvandeven@gmail.com>


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

#include <malloc.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif


extern void try_to_get(const char *path, int pid, time_t timestamp);



static int xmp_getattr(const char *path, struct stat *stbuf)
{
	int res;
	char *newpath = NULL;
	
	newpath = NULL;
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	memset(stbuf, 0, sizeof(struct stat));
	res = lstat(newpath, stbuf);

	
	/* 
	 * get the file. if the st_mtime is set, this is just an async refresh, otherwise it's
	 * a synchronous request.
	 */
	try_to_get(path, fuse_get_context()->pid, stbuf->st_mtime);

	res = lstat(newpath, stbuf);
	free(newpath);

	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_access(const char *path, int mask)
{
	int res;

	char *newpath = NULL;
	
	newpath = NULL;
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	res = access(newpath, mask);
	free(newpath);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
	int res;

	char *newpath = NULL;
	
	newpath = NULL;
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	res = readlink(newpath, buf, size - 1);
	free(newpath);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	DIR *dp;
	struct dirent *de;
	char *newpath;
	
	(void) offset;
	(void) fi;
	
	newpath = NULL;
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;
	
	dp = opendir(newpath);
	if (dp == NULL) {
		free(newpath);
		return -errno;
	}

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);
	free(newpath);
	return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;

	char *newpath = NULL;
	
	newpath = NULL;
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	if (S_ISREG(mode)) {
		res = open(newpath, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(newpath, mode);
	else
		res = mknod(newpath, mode, rdev);
	free(newpath);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
	int res;

	char *newpath = NULL;
	
	newpath = NULL;
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	res = mkdir(newpath, mode);
	free(newpath);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_unlink(const char *path)
{
	int res;

	char *newpath = NULL;
	
	newpath = NULL;
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	res = unlink(newpath);
	free(newpath);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rmdir(const char *path)
{
	int res;

	char *newpath = NULL;
	
	newpath = NULL;
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	res = rmdir(newpath);
	free(newpath);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
	int res;

	char *newfrom = NULL, *newto = NULL;
	
	if (asprintf(&newfrom, ".%s", from) < 0)
		return -ENOMEM;
	if (asprintf(&newto, ".%s", to) < 0) {
		free(newfrom);
		return -ENOMEM;
	}

	res = symlink(newfrom, newto);
	free(newfrom);
	free(newto);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rename(const char *from, const char *to)
{
	int res;

	char *newfrom = NULL, *newto = NULL;
	
	if (asprintf(&newfrom, ".%s", from) < 0)
		return -ENOMEM;
	if (asprintf(&newto, ".%s", to) < 0) {
		free(newfrom);
		return -ENOMEM;
	}

	res = rename(newfrom, newto);
	free(newfrom);
	free(newto);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_link(const char *from, const char *to)
{
	int res;

	char *newfrom = NULL, *newto = NULL;
	
	if (asprintf(&newfrom, ".%s", from) < 0)
		return -ENOMEM;
	if (asprintf(&newto, ".%s", to) < 0) {
		free(newfrom);
		return -ENOMEM;
	}

	res = link(newfrom, newto);
	free(newfrom);
	free(newto);
	
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
	int res;
	char *newpath = NULL;
	
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	res = chmod(newpath, mode);
	free(newpath);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;
	char *newpath = NULL;
	
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	res = lchown(newpath, uid, gid);
	free(newpath);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_truncate(const char *path, off_t size)
{
	int res;
	char *newpath = NULL;
	
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	res = truncate(newpath, size);
	free(newpath);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2])
{
	int res;
	char *newpath = NULL;
	
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, newpath, ts, AT_SYMLINK_NOFOLLOW);
	free(newpath);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
	int res;
	char *newpath = NULL;
	
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	res = open(newpath, fi->flags);
	free(newpath);
	if (res == -1)
		return -errno;

	close(res);
	return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int fd;
	int res;
	char *newpath = NULL;
	
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	(void) fi;
	fd = open(newpath, O_RDONLY);
	free(newpath);
	if (fd == -1)
		return -errno;

	res = pread(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	close(fd);
	return res;
}

static int xmp_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int fd;
	int res;
	char *newpath = NULL;
	
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	(void) fi;
	fd = open(newpath, O_WRONLY);
	free(newpath);
	if (fd == -1)
		return -errno;

	res = pwrite(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	close(fd);
	return res;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	int res;
	char *newpath = NULL;
	
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	res = statvfs(newpath, stbuf);
	free(newpath);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) fi;
	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int xmp_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	int fd;
	int res;
	char *newpath = NULL;
	

	(void) fi;

	if (mode)
		return -EOPNOTSUPP;
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	fd = open(newpath, O_WRONLY);
	free(newpath);
	if (fd == -1)
		return -errno;

	res = -posix_fallocate(fd, offset, length);

	close(fd);
	return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{

	char *newpath = NULL;
	
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;
	int res = lsetxattr(newpath, name, value, size, flags);
	free(newpath);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	char *newpath = NULL;
	
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;
	int res = lgetxattr(newpath, name, value, size);
	free(newpath);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
	char *newpath = NULL;
	
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;

	int res = llistxattr(newpath, list, size);
	free(newpath);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
	char *newpath = NULL;
	
	if (asprintf(&newpath, ".%s", path) < 0)
		return -ENOMEM;
	int res = lremovexattr(newpath, name);
	free(newpath);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

int save_dir;

static void*  xmp_init(struct fuse_conn_info *conn)
{
	fchdir(save_dir);
	close(save_dir);
	     
	return NULL;
}

static struct fuse_operations xmp_oper = {
	.init		= xmp_init,
	.getattr	= xmp_getattr,
	.access		= xmp_access,
	.readlink	= xmp_readlink,
	.readdir	= xmp_readdir,
	.mknod		= xmp_mknod,
	.mkdir		= xmp_mkdir,
	.symlink	= xmp_symlink,
	.unlink		= xmp_unlink,
	.rmdir		= xmp_rmdir,
	.rename		= xmp_rename,
	.link		= xmp_link,
	.chmod		= xmp_chmod,
	.chown		= xmp_chown,
	.truncate	= xmp_truncate,
#ifdef HAVE_UTIMENSAT
	.utimens	= xmp_utimens,
#endif
	.open		= xmp_open,
	.read		= xmp_read,
	.write		= xmp_write,
	.statfs		= xmp_statfs,
	.release	= xmp_release,
	.fsync		= xmp_fsync,
#ifdef HAVE_POSIX_FALLOCATE
	.fallocate	= xmp_fallocate,
#endif
#ifdef HAVE_SETXATTR
	.setxattr	= xmp_setxattr,
	.getxattr	= xmp_getxattr,
	.listxattr	= xmp_listxattr,
	.removexattr	= xmp_removexattr,
#endif
};

extern char *prefix;

int main(int argc, char *argv[])
{
	char * fake_argv[20];
	char *dir = "/usr/src/debug";
	char *shadowdir = "/var/cache/debuginfo/src";
	umask(0);
	
	/* give the system some time to boot before we go active; this is a background task */
	sleep(1);

	if (access("/sys/module/fuse/", F_OK))
		system("modprobe fuse");
	signal(SIGPIPE,SIG_IGN);
	
	if (access("/var/cache/debuginfo/lib/", F_OK))
		system("mkdir -p /var/cache/debuginfo/lib &> /dev/null");
	if (access("/var/cache/debuginfo/src/", F_OK))
		system("mkdir -p /var/cache/debuginfo/src &> /dev/null");

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
