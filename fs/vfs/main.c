/*
 * Copyright (c) 2005-2007, Kohsuke Ohtani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/statvfs.h>
#include <sys/stat.h>

#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#define open __open_variadic
#define fcntl __fcntl_variadic
#include <fcntl.h>
#undef open
#undef fcntl

#include <osv/prex.h>
#include <osv/vnode.h>
#include <osv/debug.h>

#include "vfs.h"

#include "libc.h"

#ifdef DEBUG_VFS
int	vfs_debug = VFSDB_FLAGS;
#endif

struct task *main_task;	/* we only have a single process */

int open(const char *pathname, int flags, mode_t mode)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	file_t fp;
	int fd, error;
	int acc;

	/* Find empty slot for file descriptor. */
	if ((fd = task_newfd(t)) == -1) {
		errno = EMFILE;
		return -1;
	}

	acc = 0;
	switch (flags & O_ACCMODE) {
	case O_RDONLY:
		acc = VREAD;
		break;
	case O_WRONLY:
		acc = VWRITE;
		break;
	case O_RDWR:
		acc = VREAD | VWRITE;
		break;
	}

	if ((error = task_conv(t, pathname, acc, path)) != 0)
		goto out_errno;

	if ((error = sys_open(path, flags, mode, &fp)) != 0)
		goto out_errno;

	t->t_ofile[fd] = fp;
	t->t_nopens++;
	return fd;
out_errno:
	errno = error;
	return -1;
}

int open64(const char *pathname, int flags, mode_t mode) __attribute__((alias("open")));

int creat(const char *pathname, mode_t mode)
{
	return open(pathname, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

int close(int fd)
{
	struct task *t = main_task;
	file_t fp;
	int error;

	error = EBADF;

	if (fd >= OPEN_MAX)
		goto out_errno;
	fp = t->t_ofile[fd];
	if (fp == NULL)
		goto out_errno;

	if ((error = sys_close(fp)) != 0)
		goto out_errno;

	t->t_ofile[fd] = NULL;
	t->t_nopens--;
	return 0;
out_errno:
	errno = error;
	return -1;
}

int mknod(const char *pathname, mode_t mode, dev_t dev)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	if ((error = task_conv(t, pathname, VWRITE, path)) != 0)
		goto out_errno;

	error = sys_mknod(path, mode);
	if (error)
		goto out_errno;
	return 0;

out_errno:
	errno = error;
	return -1;
}

off_t lseek(int fd, off_t offset, int whence)
{
	struct task *t = main_task;
	file_t fp;
	off_t org;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_lseek(fp, offset, whence, &org);
	if (error)
		goto out_errno;
	return org;
out_errno:
	errno = error;
	return -1;
}

typedef uint64_t off64_t;
off_t lseek64(int fd, off64_t offset, int whence)
    __attribute__((alias("lseek")));

ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
	struct task *t = main_task;
	struct iovec iov = {
		.iov_base	= buf,
		.iov_len	= count,
	};
	file_t fp;
	size_t bytes;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_read(fp, &iov, 1, offset, &bytes);
	if (error)
		goto out_errno;

	return bytes;
out_errno:
	errno = error;
	return -1;
}

ssize_t read(int fd, void *buf, size_t count)
{
	return pread(fd, buf, count, -1);
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
	struct task *t = main_task;
	struct iovec iov = {
		.iov_base	= (void *)buf,
		.iov_len	= count,
	};
	file_t fp;
	size_t bytes;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_write(fp, &iov, 1, offset, &bytes);
	if (error)
		goto out_errno;
	return bytes;
out_errno:
	errno = error;
	return -1;
}

ssize_t write(int fd, const void *buf, size_t count)
{
	return pwrite(fd, buf, count, -1);
}

ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
	struct task *t = main_task;
	file_t fp;
	size_t bytes;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_read(fp, (struct iovec *)iov, iovcnt, offset, &bytes);
	if (error)
		goto out_errno;

	return bytes;
out_errno:
	errno = error;
	return -1;
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
	return preadv(fd, iov, iovcnt, -1);
}

ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
	struct task *t = main_task;
	file_t fp;
	size_t bytes;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_write(fp, (struct iovec *)iov, iovcnt, offset, &bytes);
	if (error)
		goto out_errno;
	return bytes;
out_errno:
	errno = error;
	return -1;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
	return pwritev(fd, iov, iovcnt, -1);
}

int ioctl(int fd, int request, unsigned long arg)
{
	struct task *t = main_task;
	file_t fp;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_ioctl(fp, request, (void *)arg);
	if (error)
		goto out_errno;
	return 0;
out_errno:
	errno = error;
	return -1;
}

int fsync(int fd)
{
	struct task *t = main_task;
	file_t fp;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_fsync(fp);
	if (error)
		goto out_errno;
	return 0;

out_errno:
	errno = error;
	return -1;
}

int __fxstat(int ver, int fd, struct stat *st)
{
	struct task *t = main_task;
	file_t fp;
	int error;

	error = ENOSYS;
	if (ver != 1)
		goto out_errno;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_fstat(fp, st);
	if (error)
		goto out_errno;
	return 0;
out_errno:
	errno = error;
	return -1;
}
LFS64(__fxstat);

int fstat(int fd, struct stat *st)
{
	return __fxstat(1, fd, st);
}
LFS64(fstat);

#if 0
static int
fs_opendir(struct task *t, struct open_msg *msg)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	file_t fp;
	int fd, error;

	/* Find empty slot for file descriptor. */
	if ((fd = task_newfd(t)) == -1)
		return EMFILE;

	/* Get the mounted file system and node */
	if ((error = task_conv(t, msg->path, VREAD, path)) != 0)
		return error;

	if ((error = sys_opendir(path, &fp)) != 0)
		return error;
	t->t_ofile[fd] = fp;
	msg->fd = fd;
	return 0;
}

static int
fs_closedir(struct task *t, struct msg *msg)
{
	struct task *t = main_task;
	file_t fp;
	int fd, error;

	fd = msg->data[0];
	if (fd >= OPEN_MAX)
		return EBADF;
	fp = t->t_ofile[fd];
	if (fp == NULL)
		return EBADF;

	if ((error = sys_closedir(fp)) != 0)
		return error;
	t->t_ofile[fd] = NULL;
	return 0;
}
#endif

int
ll_readdir(int fd, struct dirent *d)
{
	struct task *t = main_task;
	int error;
	file_t fp;

	error = -EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_readdir(fp, d);
	if (error)
		goto out_errno;
	return 0;
out_errno:
	errno = error;
	return -1;
}

#if 0
static int
fs_rewinddir(struct task *t, struct msg *msg)
{
	struct task *t = main_task;
	file_t fp;

	if ((fp = task_getfp(t, msg->data[0])) == NULL)
		return EBADF;

	return sys_rewinddir(fp);
}

static int
fs_seekdir(struct task *t, struct msg *msg)
{
	struct task *t = main_task;
	file_t fp;
	long loc;

	if ((fp = task_getfp(t, msg->data[0])) == NULL)
		return EBADF;
	loc = msg->data[1];

	return sys_seekdir(fp, loc);
}

static int
fs_telldir(struct task *t, struct msg *msg)
{
	struct task *t = main_task;
	file_t fp;
	long loc;
	int error;

	if ((fp = task_getfp(t, msg->data[0])) == NULL)
		return EBADF;
	loc = msg->data[1];

	if ((error = sys_telldir(fp, &loc)) != 0)
		return error;
	msg->data[0] = loc;
	return 0;
}
#endif

int
mkdir(const char *pathname, mode_t mode)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	if ((error = task_conv(t, pathname, VWRITE, path)) != 0)
		goto out_errno;

	error = sys_mkdir(path, mode);
	if (error)
		goto out_errno;

	return 0;
out_errno:
	errno = error;
	return -1;
}

int rmdir(const char *pathname)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	error = ENOENT;
	if (pathname == NULL)
		goto out_errno;
	if ((error = task_conv(t, pathname, VWRITE, path)) != 0)
		goto out_errno;

	error = sys_rmdir(path);
	if (error)
		goto out_errno;
	return 0;
out_errno:
	errno = error;
	return -1;
}

int rename(const char *oldpath, const char *newpath)
{
	struct task *t = main_task;
	char src[PATH_MAX];
	char dest[PATH_MAX];
	int error;

	error = ENOENT;
	if (oldpath == NULL || newpath == NULL)
		goto out_errno;

	if ((error = task_conv(t, oldpath, VREAD, src)) != 0)
		goto out_errno;

	if ((error = task_conv(t, newpath, VWRITE, dest)) != 0)
		goto out_errno;

	error = sys_rename(src, dest);
	if (error)
		goto out_errno;
	return 0;
out_errno:
	errno = error;
	return -1;
}

int chdir(const char *pathname)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	file_t fp;
	int error;

	error = ENOENT;
	if (pathname == NULL)
		goto out_errno;

	/* Check if directory exits */
//	if ((error = sys_opendir(path, &fp)) != 0)
	if ((error = sys_open(path, O_RDONLY, 0, &fp)) != 0)
		goto out_errno;

	if (t->t_cwdfp)
//		sys_closedir(t->t_cwdfp);
		sys_close(t->t_cwdfp);
	t->t_cwdfp = fp;
	strlcpy(t->t_cwd, path, sizeof(t->t_cwd));
 	return 0;
out_errno:
	errno = error;
	return -1;
}

int fchdir(int fd)
{
	struct task *t = main_task;
	file_t fp;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	if (t->t_cwdfp)
//		sys_closedir(t->t_cwdfp);
		sys_close(t->t_cwdfp);
	t->t_cwdfp = fp;
	error = sys_fchdir(fp, t->t_cwd);
	if (error)
		goto out_errno;
	return 0;
out_errno:
	errno = error;
	return -1;
}

int link(const char *oldpath, const char *newpath)
{
	/* XXX */
	errno = EPERM;
	return -1;
}

int unlink(const char *pathname)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	error = ENOENT;
	if (pathname == NULL)
		goto out_errno;
	if ((error = task_conv(t, pathname, VWRITE, path)) != 0)
		goto out_errno;

	error = sys_unlink(path);
	if (error)
		goto out_errno;
	return 0;
out_errno:
	errno = error;
	return -1;
}

int __xstat(int ver, const char *pathname, struct stat *st)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	error = ENOSYS;
	if (ver != 1)
		goto out_errno;

	error = task_conv(t, pathname, 0, path);
	if (error)
		goto out_errno;

	error = sys_stat(path, st);
	if (error)
		goto out_errno;
	return 0;

out_errno:
	errno = error;
	return -1;
}
LFS64(__xstat);

int stat(const char *pathname, struct stat *st)
{
	return __xstat(1, pathname, st);
}
LFS64(stat);

int __lxstat(int ver, const char *pathname, struct stat *st)
{
	return __xstat(ver, pathname, st);
}
LFS64(__lxstat);

int lstat(const char *pathname, struct stat *st)
{
	return __lxstat(1, pathname, st);
}
LFS64(lstat);

int __statfs(const char *pathname, struct statfs *buf)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	error = task_conv(t, pathname, 0, path);
	if (error)
		goto out_errno;

	error = sys_statfs(path, buf);
	if (error)
		goto out_errno;
	return 0;

out_errno:
	errno = error;
	return -1;
}
weak_alias(__statfs, statfs);
LFS64(statfs);

int __fstatfs(int fd, struct statfs *buf)
{
	struct task *t = main_task;
	struct file *fp;
	int error;

	error = EBADF;
	fp = task_getfp(t, fd);
	if (!fp)
		goto out_errno;

	error = sys_fstatfs(fp, buf);
	if (error)
		goto out_errno;
	return 0;
out_errno:
	errno = error;
	return -1;
}
weak_alias(__fstatfs, fstatfs);
LFS64(fstatfs);

static int
statfs_to_statvfs(struct statvfs *dst, struct statfs *src)
{
	dst->f_bsize = src->f_bsize;
	dst->f_frsize = src->f_bsize;
	dst->f_blocks = src->f_blocks;
	dst->f_bfree = src->f_bfree;
	dst->f_bavail = src->f_bavail;
	dst->f_files = src->f_files;
	dst->f_ffree = src->f_ffree;
	dst->f_favail = 0;
	dst->f_fsid = src->f_fsid.__val[0];
	dst->f_flag = src->f_flags;
	dst->f_namemax = src->f_namelen;
	return 0;
}

int
statvfs(const char *pathname, struct statvfs *buf)
{
	struct statfs st;

	if (__statfs(pathname, &st) < 0)
		return -1;
	return statfs_to_statvfs(buf, &st);
}
LFS64(statvfs);

int
fstatvfs(int fd, struct statvfs *buf)
{
	struct statfs st;

	if (__fstatfs(fd, &st) < 0)
		return -1;
	return statfs_to_statvfs(buf, &st);
}
LFS64(fstatvfs);


char *getcwd(char *path, size_t size)
{
	struct task *t = main_task;
	int len = strlen(t->t_cwd) + 1;
	int error;

	if (!path) {
		if (!size)
			size = len;
		path = malloc(size);
		if (!path) {
			error = ENOMEM;
			goto out_errno;
		}
	} else {
		if (!size) {
			error = EINVAL;
			goto out_errno;
		}
	}

	if (size < len) {
		error = ERANGE;
		goto out_errno;
	}

	memcpy(path, t->t_cwd, len);
	return path;

out_errno:
	errno = error;
	return NULL;
}

/*
 * Duplicate a file descriptor
 */
int dup(int oldfd)
{
	struct task *t = main_task;
	file_t fp;
	int newfd;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, oldfd)) == NULL)
		goto out_errno;

	/* Find smallest empty slot as new fd. */
	error = EMFILE;
	if ((newfd = task_newfd(t)) == -1)
		goto out_errno;

	t->t_ofile[newfd] = fp;

	/* Increment file reference */
	fp->f_count++;

	return newfd;
out_errno:
	errno = error;
	return -1;
}

/*
 * Duplicate a file descriptor to a particular value.
 */
int dup3(int oldfd, int newfd, int flags)
{
	struct task *t = main_task;
	file_t fp, org;
	int error;

	/*
	 * Don't allow any argument but O_CLOEXEC.  But we even ignore
	 * that as we don't support exec() and thus don't care.
	 */
	if ((flags & ~O_CLOEXEC) != 0)
		return -EINVAL;

	error = EBADF;
	if (oldfd >= OPEN_MAX || newfd >= OPEN_MAX)
		goto out_errno;
	fp = t->t_ofile[oldfd];
	if (fp == NULL)
		goto out_errno;
	org = t->t_ofile[newfd];
	if (org != NULL) {
		/* Close previous file if it's opened. */
		error = sys_close(org);
	}
	t->t_ofile[newfd] = fp;

	/* Increment file reference */
	fp->f_count++;
	return newfd;
out_errno:
	errno = error;
	return -1;
}

int dup2(int oldfd, int newfd)
{
	return dup3(oldfd, newfd, 0);
}

/*
 * The file control system call.
 */
int fcntl(int fd, int cmd, int arg)
{
	struct task *t = main_task;
	file_t fp;
	int new_fd;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	switch (cmd) {
	case F_DUPFD:
		if (arg >= OPEN_MAX)
			return EINVAL;
		/* Find smallest empty slot as new fd. */
		if ((new_fd = task_newfd(t)) == -1)
			return EMFILE;
		t->t_ofile[new_fd] = fp;

		/* Increment file reference */
		fp->f_count++;
		return new_fd;
	case F_GETFD:
		return fp->f_flags & FD_CLOEXEC;
	case F_SETFD:
		fp->f_flags = (fp->f_flags & ~FD_CLOEXEC) |
			(arg & FD_CLOEXEC);
		return 0;
	default:
		kprintf("unsupported fcntl cmd 0x%x\n", cmd);
		error = EINVAL;
		goto out_errno;
	}
	return 0;
out_errno:
	errno = error;
	return -1;
}

/*
 * Check permission for file access
 */
int access(const char *pathname, int mode)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int acc, error = 0;

	acc = 0;
	if (mode & R_OK)
		acc |= VREAD;
	if (mode & W_OK)
		acc |= VWRITE;

	if ((error = task_conv(t, pathname, acc, path)) != 0)
		goto out_errno;

	error = sys_access(path, mode);
	if (error)
		goto out_errno;
	return 0;
out_errno:
	errno = error;
	return -1;
}

#if 0
static int
fs_pipe(struct task *t, struct msg *msg)
{
#ifdef CONFIG_FIFOFS
	char path[PATH_MAX];
	file_t rfp, wfp;
	int error, rfd, wfd;

	DPRINTF(VFSDB_CORE, ("fs_pipe\n"));

	if ((rfd = task_newfd(t)) == -1)
		return EMFILE;
	t->t_ofile[rfd] = (file_t)1; /* temp */

	if ((wfd = task_newfd(t)) == -1) {
		t->t_ofile[rfd] = NULL;
		return EMFILE;
	}
	sprintf(path, "/mnt/fifo/pipe-%x-%d", (u_int)t->t_taskid, rfd);

	if ((error = sys_mknod(path, S_IFIFO)) != 0)
		goto out;
	if ((error = sys_open(path, O_RDONLY | O_NONBLOCK, 0, &rfp)) != 0) {
		goto out;
	}
	if ((error = sys_open(path, O_WRONLY | O_NONBLOCK, 0, &wfp)) != 0) {
		goto out;
	}
	t->t_ofile[rfd] = rfp;
	t->t_ofile[wfd] = wfp;
	t->t_nopens += 2;
	msg->data[0] = rfd;
	msg->data[1] = wfd;
	return 0;
 out:
	t->t_ofile[rfd] = NULL;
	t->t_ofile[wfd] = NULL;
	return error;
#else
	return ENOSYS;
#endif
}
#endif

/*
 * Return if specified file is a tty
 */
int isatty(int fd)
{
	struct task *t = main_task;
	file_t fp;
	int istty = 0;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	if (fp->f_vnode->v_flags & VISTTY)
		istty = 1;
	return istty;
out_errno:
	errno = error;
	return -1;
}

int truncate(const char *pathname, off_t length)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	error = ENOENT;
	if (pathname == NULL)
		goto out_errno;
	if ((error = task_conv(t, pathname, VWRITE, path)) != 0)
		goto out_errno;

	error = sys_truncate(path, length);
	if (error)
		goto out_errno;
	return 0;
out_errno:
	errno = error;
	return -1;
}

int ftruncate(int fd, off_t length)
{
	struct task *t = main_task;
	file_t fp;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_ftruncate(fp, length);
	if (error)
		goto out_errno;
	return 0;
out_errno:
	errno = error;
	return -1;
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsize)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	error = -EINVAL;
	if (bufsize <= 0)
		goto out_errno;

	error = ENOENT;
	if (pathname == NULL)
		goto out_errno;
	error = task_conv(t, pathname, VWRITE, path);
	if (error)
		goto out_errno;

	error = sys_readlink(path, buf, bufsize);
	if (error)
		goto out_errno;
	return 0;
out_errno:
	errno = error;
	return -1;
}

int
fs_noop(void)
{
	return 0;
}

#ifdef DEBUG_VFS
/*
 * Dump internal data.
 */
static int
fs_debug(struct task *t, struct msg *msg)
{

	kprintf("<File System Server>\n");
	vnode_dump();
	mount_dump();
	return 0;
}
#endif

struct bootfs_metadata {
	uint64_t size;
	uint64_t offset;
	char name[112];
};

extern char bootfs_start;

void unpack_bootfs(void)
{
	struct bootfs_metadata *md = (struct bootfs_metadata *)&bootfs_start;
	int fd, i;
	const char *dirs[] = {	// XXX: derive from bootfs contents
		"/usr",
		"/usr/lib",
		"/usr/lib/jvm",
		"/usr/lib/jvm/jre",
		"/usr/lib/jvm/jre/lib",
		"/usr/lib/jvm/jre/lib/amd64",
		"/usr/lib/jvm/jre/lib/amd64/server",
		"/java",
		"/tests",
		"/tmp",
		NULL,
	};

	for (i = 0; dirs[i] != NULL; i++) {
		if (mkdir(dirs[i], 0666) < 0) {
			perror("mkdir");
			sys_panic("foo");
		}
	}

	for (i = 0; md[i].name[0]; i++) {
		int ret;

		fd = creat(md[i].name, 0666);
		if (fd < 0) {
			kprintf("couldn't create %s: %d\n",
				md[i].name, errno);
			sys_panic("foo");
		}

		ret = write(fd, &bootfs_start + md[i].offset, md[i].size);
		if (ret != md[i].size) {
			kprintf("write failed, ret = %d, errno = %d\n",
				ret, errno);
			sys_panic("foo");
		}

		close(fd);
	}
}

void mount_rootfs(void)
{
	int ret;

	ret = sys_mount("", "/", "ramfs", 0, NULL);
	if (ret)
		kprintf("failed to mount rootfs, error = %d\n", ret);

	if (mkdir("/dev", 0755) < 0)
		kprintf("failed to create /dev, error = %d\n", errno);

	ret = sys_mount("", "/dev", "devfs", 0, NULL);
	if (ret)
		kprintf("failed to mount devfs, error = %d\n", ret);

}

int console_init(void);
void bio_init(void);

int vfs_initialized;

void
vfs_init(void)
{
	const struct vfssw *fs;

	bio_init();
	vnode_init();
	task_alloc(&main_task);
	console_init();

	/*
	 * Initialize each file system.
	 */
	for (fs = vfssw; fs->vs_name; fs++) {
		DPRINTF(VFSDB_CORE, ("VFS: initializing %s\n",
				     fs->vs_name));
		fs->vs_init();
	}

	mount_rootfs();
	unpack_bootfs();

	if (open("/dev/console", O_RDWR, 0) != 0)
		kprintf("failed to open console, error = %d\n", errno);
	if (dup(0) != 1)
		kprintf("failed to dup console (1)\n");
	if (dup(0) != 2)
		kprintf("failed to dup console (2)\n");
	vfs_initialized = 1;
}

void sys_panic(const char *str)
{
	kprintf(str);
	while (1)
		;
}

