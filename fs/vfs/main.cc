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
#include <osv/ioctl.h>
#include <osv/trace.hh>

#include "vfs.h"

#include "libc/internal/libc.h"

#ifdef DEBUG_VFS
int	vfs_debug = VFSDB_FLAGS;
#endif

tracepoint<const char*, int> trace_open("open", "%s %x");
tracepoint<int> trace_open_fd("open_fd", "%d");
tracepoint<int> trace_open_err("open_ret", "%d");

struct task *main_task;	/* we only have a single process */

extern "C"
int open(const char *pathname, int flags, mode_t mode)
{
	trace_open(pathname, flags);

	struct task *t = main_task;
	char path[PATH_MAX];
	struct file *fp;
	int fd, error;
	int acc;

	error = falloc(&fp, &fd);
	if (error)
		goto out_errno;

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

	error = task_conv(t, pathname, acc, path);
	if (error)
		goto out_fput;

	error = sys_open(path, flags, mode, fp);
	if (error)
		goto out_fput;

	fdrop(fp);
	trace_open_fd(fd);
	return fd;

out_fput:
	fdrop(fp);
	fdclose(fd);
out_errno:
	errno = error;
	trace_open_err(error);
	return -1;
}

extern "C" int open64(const char *pathname, int flags, ...) __attribute__((alias("open")));

int creat(const char *pathname, mode_t mode)
{
	return open(pathname, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

int close(int fd)
{
	int error;

	error = fdclose(fd);
	if (error)
		goto out_errno;

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
	struct file *fp;
	off_t org;
	int error;

	error = fget(fd, &fp);
	if (error)
		goto out_errno;

	error = sys_lseek(fp, offset, whence, &org);
	fdrop(fp);

	if (error)
		goto out_errno;
	return org;

out_errno:
	errno = error;
	return -1;
}

extern "C"
off_t lseek64(int fd, off64_t offset, int whence)
    __attribute__((alias("lseek")));

ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
	struct iovec iov = {
		.iov_base	= buf,
		.iov_len	= count,
	};
	struct file *fp;
	size_t bytes;
	int error;

	error = fget(fd, &fp);
	if (error)
		goto out_errno;

	error = sys_read(fp, &iov, 1, offset, &bytes);
	fdrop(fp);

	if (error)
		goto out_errno;
	return bytes;

out_errno:
	errno = error;
	return -1;
}

ssize_t pread64(int, const void*, size_t, off_t) __attribute__((weak, alias("pread")));

ssize_t read(int fd, void *buf, size_t count)
{
	return pread(fd, buf, count, -1);
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
	struct iovec iov = {
		.iov_base	= (void *)buf,
		.iov_len	= count,
	};
	struct file *fp;
	size_t bytes;
	int error;

	error = fget(fd, &fp);
	if (error)
		goto out_errno;

	error = sys_write(fp, &iov, 1, offset, &bytes);
	fdrop(fp);

	if (error)
		goto out_errno;
	return bytes;

out_errno:
	errno = error;
	return -1;
}

ssize_t pwrite64(int, const void*, size_t, off_t) __attribute__((weak, alias("pwrite")));

ssize_t write(int fd, const void *buf, size_t count)
{
	return pwrite(fd, buf, count, -1);
}

ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
	struct file *fp;
	size_t bytes;
	int error;

	error = fget(fd, &fp);
	if (error)
		goto out_errno;

	error = sys_read(fp, (struct iovec *)iov, iovcnt, offset, &bytes);
	fdrop(fp);

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
	struct file *fp;
	size_t bytes;
	int error;

	error = fget(fd, &fp);
	if (error)
		goto out_errno;

	error = sys_write(fp, (struct iovec *)iov, iovcnt, offset, &bytes);
	fdrop(fp);

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

int ioctl(int fd, unsigned long int request, ...)
{
	struct file *fp;
	int error;
	va_list ap;
	void* arg;

	/* glibc ABI provides a variadic prototype for ioctl so we need to agree
	 * with it, since we now include sys/ioctl.h
	 * read the first argument and pass it to sys_ioctl() */
	va_start(ap, request);
	arg = va_arg(ap, void*);
	va_end(ap);

	error = fget(fd, &fp);
	if (error)
		goto out_errno;

	error = sys_ioctl(fp, request, arg);
	fdrop(fp);

	if (error)
		goto out_errno;
	return 0;

out_errno:
	errno = error;
	return -1;
}

int fsync(int fd)
{
	struct file *fp;
	int error;

	error = fget(fd, &fp);
	if (error)
		goto out_errno;

	error = sys_fsync(fp);
	fdrop(fp);

	if (error)
		goto out_errno;
	return 0;

out_errno:
	errno = error;
	return -1;
}

int fdatasync(int fd)
{
    // TODO: See if we can do less than fsync().
    return fsync(fd);
}

int __fxstat(int ver, int fd, struct stat *st)
{
	struct file *fp;
	int error;

	error = ENOSYS;
	if (ver != 1)
		goto out_errno;

	error = fget(fd, &fp);
	if (error)
		goto out_errno;

	error = sys_fstat(fp, st);
	fdrop(fp);

	if (error)
		goto out_errno;
	return 0;

out_errno:
	errno = error;
	return -1;
}
int __fxstat64(int, int, struct stat64 *) __attribute__((weak, alias("__fxstat")));

extern "C"
int fstat(int fd, struct stat *st)
{
	return __fxstat(1, fd, st);
}
int fstat64(int, struct stat64 *) __attribute__((weak, alias("fstat")));

extern "C"
int
ll_readdir(int fd, struct dirent *d)
{
	int error;
	struct file *fp;

	error = fget(fd, &fp);
	if (error)
		goto out_errno;

	error = sys_readdir(fp, d);
	fdrop(fp);

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

	error = fget(fd, &fp);
	if (error)
		return EBADF;

	return sys_rewinddir(fp);
}

static int
fs_seekdir(struct task *t, struct msg *msg)
{
	struct task *t = main_task;
	file_t fp;
	long loc;

	error = fget(fd, &fp);
	if (error)
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

	error = fget(fd, &fp);
	if (error)
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
	struct file *fp, *old = NULL;
	int error;

	error = ENOENT;
	if (pathname == NULL)
		goto out_errno;

	error = falloc_noinstall(&fp);
	if (error)
		goto out_errno;

	/* Check if directory exits */
	error = sys_open(path, O_RDONLY, 0, fp);
	if (error) {
		fdrop(fp);
		goto out_errno;
	}

	if (t->t_cwdfp)
		old = t->t_cwdfp;
	t->t_cwdfp = fp;
	strlcpy(t->t_cwd, path, sizeof(t->t_cwd));

	if (old)
		fdrop(old);
 	return 0;
out_errno:
	errno = error;
	return -1;
}

int fchdir(int fd)
{
	struct task *t = main_task;
	struct file *fp, *old = NULL;
	int error;

	error = fget(fd, &fp);
	if (error)
		goto out_errno;

	if (t->t_cwdfp)
		old = t->t_cwdfp;

	error = sys_fchdir(fp, t->t_cwd);
	if (error) {
		fdrop(fp);
		goto out_errno;
	}

	t->t_cwdfp = fp;
	if (old)
		fdrop(old);
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

tracepoint<const char*> trace_stat("stat", "%s");
tracepoint<int> trace_stat_err("stat_err", "%d");

int __xstat(int ver, const char *pathname, struct stat *st)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	trace_stat(pathname);
	error = ENOSYS;
	if (ver != 1)
		goto out_errno;

	error = task_conv(t, pathname, 0, path);
	if (error)
		goto out_errno;

	error = sys_stat(path, st);
	if (error)
		goto out_errno;
	trace_stat_err(0);
	return 0;

out_errno:
	trace_stat_err(error);
	errno = error;
	return -1;
}
int __xstat64(int, const char *, struct stat64 *) __attribute__((weak, alias("__xstat")));

int stat(const char *pathname, struct stat *st)
{
	return __xstat(1, pathname, st);
}
int stat64(const char *, struct stat64 *) __attribute__((weak, alias("stat")));

int __lxstat(int ver, const char *pathname, struct stat *st)
{
	return __xstat(ver, pathname, st);
}
int __lxstat64(int, const char *, struct stat64 *) __attribute__((weak, alias("__lxstat")));

int lstat(const char *pathname, struct stat *st)
{
	return __lxstat(1, pathname, st);
}
int lstat64(const char *, struct stat64 *) __attribute__((weak, alias("lstat")));

extern "C"
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
int statfs64(const char *, struct statfs64 *) __attribute__((weak, alias("statfs")));

extern "C"
int __fstatfs(int fd, struct statfs *buf)
{
	struct file *fp;
	int error;

	error = fget(fd, &fp);
	if (error)
		goto out_errno;

	error = sys_fstatfs(fp, buf);
	fdrop(fp);

	if (error)
		goto out_errno;
	return 0;

out_errno:
	errno = error;
	return -1;
}
weak_alias(__fstatfs, fstatfs);
int fstatfs64(int, struct statfs64 *) __attribute__((weak, alias("fstatfs")));

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
int statvfs64(const char *, struct statvfs64 *) __attribute__((weak, alias("statvfs")));

int
fstatvfs(int fd, struct statvfs *buf)
{
	struct statfs st;

	if (__fstatfs(fd, &st) < 0)
		return -1;
	return statfs_to_statvfs(buf, &st);
}
int fstatvfs64(int, struct statvfs64 *) __attribute__((weak, alias("statvfs")));


char *getcwd(char *path, size_t size)
{
	struct task *t = main_task;
	int len = strlen(t->t_cwd) + 1;
	int error;

	if (!path) {
		if (!size)
			size = len;
		path = (char*)malloc(size);
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
	struct file *fp;
	int newfd;
	int error;

	error = fget(oldfd, &fp);
	if (error)
		goto out_errno;

	error = fdalloc(fp, &newfd);
	if (error)
		goto out_fdrop;

	fdrop(fp);
	return newfd;

out_fdrop:
	fdrop(fp);
out_errno:
	errno = error;
	return -1;
}

/*
 * Duplicate a file descriptor to a particular value.
 */
int dup3(int oldfd, int newfd, int flags)
{
	struct file *fp;
	int error;

	/*
	 * Don't allow any argument but O_CLOEXEC.  But we even ignore
	 * that as we don't support exec() and thus don't care.
	 */
	if ((flags & ~O_CLOEXEC) != 0) {
		error = EINVAL;
		goto out_errno;
	}

	if (oldfd == newfd) {
		error = EINVAL;
		goto out_errno;
	}

	error = fget(oldfd, &fp);
	if (error)
		goto out_errno;

	error = fdset(newfd, fp);
	if (error) {
		fdrop(fp);
		goto out_errno;
	}

	fdrop(fp);
	return newfd;

out_errno:
	errno = error;
	return -1;
}

int dup2(int oldfd, int newfd)
{
	if (oldfd == newfd)
		return newfd;

	return dup3(oldfd, newfd, 0);
}

/*
 * The file control system call.
 */
#define SETFL (O_APPEND | O_NONBLOCK | O_ASYNC)
#define SETFL_IGNORED (O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC)

extern "C"
int fcntl(int fd, int cmd, int arg)
{
	struct file *fp;
	int ret = 0, error;
	int tmp;

	error = fget(fd, &fp);
	if (error)
		goto out_errno;

	switch (cmd) {
	case F_DUPFD:
		error = _fdalloc(fp, &ret, arg);
		if (error)
			goto out_errno;
		break;
	case F_GETFD:
		ret = fp->f_flags & FD_CLOEXEC;
		break;
	case F_SETFD:
		FD_LOCK(fp);
		fp->f_flags = (fp->f_flags & ~FD_CLOEXEC) |
			(arg & FD_CLOEXEC);
		FD_UNLOCK(fp);
		break;
	case F_GETFL:
		ret = fp->f_flags;
		break;
	case F_SETFL:
		/* Ignore flags */
		arg &= ~SETFL_IGNORED;

		assert((arg & ~SETFL) == 0);
		FD_LOCK(fp);
		fp->f_flags = (fp->f_flags & ~SETFL) |
			(arg & SETFL);
		FD_UNLOCK(fp);

		/* Sync nonblocking/async state with file flags */
		tmp = fp->f_flags & FNONBLOCK;
		fo_ioctl(fp, FIONBIO, &tmp);
		tmp = fp->f_flags & FASYNC;
		fo_ioctl(fp, FIOASYNC, &tmp);

		break;
	default:
		kprintf("unsupported fcntl cmd 0x%x\n", cmd);
		error = EINVAL;
	}

	fdrop(fp);
	if (error)
		goto out_errno;
	return ret;

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
	struct file *fp;
	int istty = 0;
	int error;

	error = fget(fd, &fp);
	if (error)
		goto out_errno;

	if (fp->f_vnode && fp->f_vnode->v_flags & VISTTY)
		istty = 1;
	fdrop(fp);

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
	struct file *fp;
	int error;

	error = fget(fd, &fp);
	if (error)
		goto out_errno;

	error = sys_ftruncate(fp, length);
	fdrop(fp);

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

int chmod(const char *pathname, mode_t mode)
{
	debug("stub chmod\n");
	return 0;
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

#define BOOTFS_PATH_MAX 112

struct bootfs_metadata {
	uint64_t size;
	uint64_t offset;
	char name[BOOTFS_PATH_MAX];
};

extern char bootfs_start;

void unpack_bootfs(void)
{
	struct bootfs_metadata *md = (struct bootfs_metadata *)&bootfs_start;
	int fd, i;

	// Empty directories to create. Other directories will be created
	// automatically below.
	const char *dirs[] = {
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

		// mkdir() directories needed for this path name, as necessary
		char tmp[BOOTFS_PATH_MAX];
		strncpy(tmp, md[i].name, BOOTFS_PATH_MAX);
		for (char *p = tmp; *p; ++p) {
		    if (*p == '/') {
		        *p = '\0';
		        mkdir(tmp, 0666);  // silently ignore errors and existing dirs
		        *p = '/';
		    }
		}

		fd = creat(md[i].name, 0666);
		if (fd < 0) {
			kprintf("couldn't create %s: %d\n",
				md[i].name, errno);
			sys_panic("unpack_bootfs failed");
		}

		ret = write(fd, &bootfs_start + md[i].offset, md[i].size);
		if (ret != md[i].size) {
			kprintf("write failed, ret = %d, errno = %d\n",
				ret, errno);
			sys_panic("unpack_bootfs failed");
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

void mount_usr(void)
{
	int ret;

	if (mkdir("/usr", 0755) < 0)
		kprintf("failed to create /usr, error = %d\n", errno);

	ret = sys_mount("/dev/vblk1", "/usr", "romfs", 0, NULL);
	if (ret)
		kprintf("failed to mount romfs, error = %d\n", ret);
}

extern "C" int console_init(void);
extern "C" void bio_init(void);

int vfs_initialized;

extern "C"
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

