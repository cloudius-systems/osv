/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

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
#include <sys/statx.h>
#include <sys/time.h>
#include <sys/sendfile.h>

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
#include <dlfcn.h>

#include <osv/prex.h>
#include <osv/export.h>
#include <osv/vnode.h>
#include <osv/stubbing.hh>
#include <osv/ioctl.h>
#include <osv/trace.hh>
#include <osv/run.hh>
#include <osv/mount.h>
#include <drivers/console.hh>

#include "vfs.h"

#include "libc/internal/libc.h"

#include <algorithm>
#include <unordered_map>

#include <sys/file.h>

#include "fs/fs.hh"
#include "libc/libc.hh"

#include <mntent.h>
#include <sys/mman.h>

#include <osv/clock.hh>
#include <api/utime.h>
#include <chrono>

#include "drivers/zfs.hh"
#include "bsd/porting/shrinker.h"

using namespace std;


#ifdef DEBUG_VFS
int	vfs_debug = VFSDB_FLAGS;
#endif

std::atomic<mode_t> global_umask{S_IWGRP | S_IWOTH};

static inline mode_t apply_umask(mode_t mode)
{
    return mode & ~global_umask.load(std::memory_order_relaxed);
}

TRACEPOINT(trace_vfs_open, "\"%s\" 0x%x 0%0o", const char*, int, mode_t);
TRACEPOINT(trace_vfs_open_ret, "%d", int);
TRACEPOINT(trace_vfs_open_err, "%d", int);

struct task *main_task;	/* we only have a single process */

extern "C" OSV_LIBC_API
int open(const char *pathname, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = apply_umask(va_arg(ap, mode_t));
        va_end(ap);
    }

    trace_vfs_open(pathname, flags, mode);

    struct task *t = main_task;
    char path[PATH_MAX];
    struct file *fp;
    int fd, error;
    int acc;

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
        goto out_errno;

    error = sys_open(path, flags, mode, &fp);
    if (error)
        goto out_errno;

    error = fdalloc(fp, &fd);
    if (error)
        goto out_fput;
    fdrop(fp);
    trace_vfs_open_ret(fd);
    return fd;

    out_fput:
    fdrop(fp);
    out_errno:
    errno = error;
    trace_vfs_open_err(error);
    return -1;
}

LFS64(open);

static int vfs_fun_at(int dirfd, const char *pathname, std::function<int(const char *)> fun)
{
    struct file *fp;
    int error = fget(dirfd, &fp);
    if (error) {
        errno = error;
        return -1;
    }

    struct vnode *vp = fp->f_dentry->d_vnode;
    vn_lock(vp);

    std::unique_ptr<char []> up (new char[PATH_MAX]);
    char *p = up.get();

    /* build absolute path */
    strlcpy(p, fp->f_dentry->d_mount->m_path, PATH_MAX);
    strlcat(p, fp->f_dentry->d_path, PATH_MAX);
    if (pathname) {
        strlcat(p, "/", PATH_MAX);
        strlcat(p, pathname, PATH_MAX);
    }

    error = fun(p);

    vn_unlock(vp);
    fdrop(fp);

    return error;
}

static int vfs_fun_at2(int dirfd, const char *pathname, std::function<int(const char *)> fun)
{
    if (!pathname) {
        errno = EINVAL;
        return -1;
    }

    if (pathname[0] == '/' || dirfd == AT_FDCWD) {
        return fun(pathname);
    }

    return vfs_fun_at(dirfd, pathname, fun);
}

OSV_LIBC_API
int openat(int dirfd, const char *pathname, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = apply_umask(va_arg(ap, mode_t));
        va_end(ap);
    }

    return vfs_fun_at2(dirfd, pathname, [flags, mode](const char *path) {
        return open(path, flags, mode);
    });
}
LFS64(openat);

// open() has an optional third argument, "mode", which is only needed in
// some cases (when the O_CREAT mode is used). As a safety feature, recent
// versions of Glibc add a feature where open() with two arguments is replaced
// by a call to __open_2(), which verifies it isn't called with O_CREATE.
extern "C" OSV_LIBC_API
int __open_2(const char *pathname, int flags)
{
    assert(!(flags & O_CREAT));
    return open(pathname, flags, 0);
}

extern "C" OSV_LIBC_API
int __open64_2(const char *file, int flags)
{
    if (flags & O_CREAT) {
        errno = EINVAL;
        return -1;
    }

    return open64(file, flags);
}

// Same missing-mode protection for openat(). Note that this is NOT the
// same as openat2()!
extern "C" OSV_LIBC_API
int __openat_2(int dirfd, const char *pathname, int flags)
{
    if (flags & O_CREAT) {
        abort("__openat_2 called for open mode 0%o with O_CREAT", flags);
    }
    return openat(dirfd, pathname, flags, 0);
}

extern "C" OSV_LIBC_API
int __openat64_2(int dirfd, const char *pathname, int flags)
{
    if (flags & O_CREAT) {
        abort("__openat64_2 called for open mode 0%o with O_CREAT", flags);
    }
    return openat64(dirfd, pathname, flags, 0);
}

OSV_LIBC_API
int creat(const char *pathname, mode_t mode)
{
    return open(pathname, O_CREAT|O_WRONLY|O_TRUNC, mode);
}
LFS64(creat);

TRACEPOINT(trace_vfs_close, "%d", int);
TRACEPOINT(trace_vfs_close_ret, "");
TRACEPOINT(trace_vfs_close_err, "%d", int);

OSV_LIBC_API
int close(int fd)
{
    int error;

    trace_vfs_close(fd);
    error = fdclose(fd);
    if (error)
        goto out_errno;

    trace_vfs_close_ret();
    return 0;

    out_errno:
    trace_vfs_close_err(error);
    errno = error;
    return -1;
}

TRACEPOINT(trace_vfs_mknod, "\"%s\" 0%0o 0x%x", const char*, mode_t, dev_t);
TRACEPOINT(trace_vfs_mknod_ret, "");
TRACEPOINT(trace_vfs_mknod_err, "%d", int);


extern "C" OSV_LIBC_API
int __xmknod(int ver, const char *pathname, mode_t mode, dev_t *dev)
{
    assert(ver == 0); // On x86-64 Linux, _MKNOD_VER_LINUX is 0.
    struct task *t = main_task;
    char path[PATH_MAX];
    int error;

    trace_vfs_mknod(pathname, mode, *dev);
    if ((error = task_conv(t, pathname, VWRITE, path)) != 0)
        goto out_errno;

    error = sys_mknod(path, mode);
    if (error)
        goto out_errno;

    trace_vfs_mknod_ret();
    return 0;

    out_errno:
    trace_vfs_mknod_err(error);
    errno = error;
    return -1;
}

OSV_LIBC_API
int mknod(const char *pathname, mode_t mode, dev_t dev)
{
    return __xmknod(0, pathname, mode, &dev);
}

OSV_LIBC_API
int mknodat(int dirfd, const char *pathname, mode_t mode, dev_t dev)
{
    return vfs_fun_at2(dirfd, pathname, [mode, dev](const char *path) {
        return mknod(path, mode, dev);
    });
}

TRACEPOINT(trace_vfs_lseek, "%d 0x%x %d", int, off_t, int);
TRACEPOINT(trace_vfs_lseek_ret, "0x%x", off_t);
TRACEPOINT(trace_vfs_lseek_err, "%d", int);

OSV_LIBC_API
off_t lseek(int fd, off_t offset, int whence)
{
    struct file *fp;
    off_t org;
    int error;

    trace_vfs_lseek(fd, offset, whence);
    error = fget(fd, &fp);
    if (error)
        goto out_errno;

    error = sys_lseek(fp, offset, whence, &org);
    fdrop(fp);

    if (error)
        goto out_errno;
    trace_vfs_lseek_ret(org);
    return org;

    out_errno:
    trace_vfs_lseek_err(error);
    errno = error;
    return -1;
}

LFS64(lseek);

TRACEPOINT(trace_vfs_pread, "%d %p 0x%x 0x%x", int, void*, size_t, off_t);
TRACEPOINT(trace_vfs_pread_ret, "0x%x", ssize_t);
TRACEPOINT(trace_vfs_pread_err, "%d", int);

// In BSD's internal implementation of read() and write() code, for example
// sosend_generic(), a partial read or write returns both an EWOULDBLOCK error
// *and* a non-zero number of written bytes. In that case, we need to zero the
// error, so the system call appear a successful partial read/write.
// In FreeBSD, dofilewrite() and dofileread() (sys_generic.c) do this too.
static inline bool has_error(int error, int bytes)
{
    return error && (
            (bytes == 0) ||
            (error != EWOULDBLOCK && error != EINTR && error != ERESTART));
}


OSV_LIBC_API
ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
    trace_vfs_pread(fd, buf, count, offset);
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

    if (has_error(error, bytes))
        goto out_errno;
    trace_vfs_pread_ret(bytes);
    return bytes;

    out_errno:
    trace_vfs_pread_err(error);
    errno = error;
    return -1;
}

LFS64(pread);

OSV_LIBC_API
ssize_t read(int fd, void *buf, size_t count)
{
    return pread(fd, buf, count, -1);
}

TRACEPOINT(trace_vfs_pwrite, "%d %p 0x%x 0x%x", int, const void*, size_t, off_t);
TRACEPOINT(trace_vfs_pwrite_ret, "0x%x", ssize_t);
TRACEPOINT(trace_vfs_pwrite_err, "%d", int);

OSV_LIBC_API
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    trace_vfs_pwrite(fd, buf, count, offset);
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

    if (has_error(error, bytes))
        goto out_errno;
    trace_vfs_pwrite_ret(bytes);
    return bytes;

    out_errno:
    trace_vfs_pwrite_err(error);
    errno = error;
    return -1;
}

LFS64(pwrite);

OSV_LIBC_API
ssize_t write(int fd, const void *buf, size_t count)
{
    return pwrite(fd, buf, count, -1);
}

OSV_LIBC_API
ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
    struct file *fp;
    size_t bytes;
    int error;

    error = fget(fd, &fp);
    if (error)
        goto out_errno;

    error = sys_read(fp, iov, iovcnt, offset, &bytes);
    fdrop(fp);

    if (has_error(error, bytes))
        goto out_errno;
    return bytes;

    out_errno:
    errno = error;
    return -1;
}

OSV_LIBC_API
ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    return preadv(fd, iov, iovcnt, -1);
}

TRACEPOINT(trace_vfs_pwritev, "%d %p 0x%x 0x%x", int, const struct iovec*, int, off_t);
TRACEPOINT(trace_vfs_pwritev_ret, "0x%x", ssize_t);
TRACEPOINT(trace_vfs_pwritev_err, "%d", int);

OSV_LIBC_API
ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
    struct file *fp;
    size_t bytes;
    int error;

    trace_vfs_pwritev(fd, iov, iovcnt, offset);
    error = fget(fd, &fp);
    if (error)
        goto out_errno;

    error = sys_write(fp, iov, iovcnt, offset, &bytes);
    fdrop(fp);

    if (has_error(error, bytes))
        goto out_errno;
    trace_vfs_pwritev_ret(bytes);
    return bytes;

    out_errno:
    trace_vfs_pwritev_err(error);
    errno = error;
    return -1;
}

OSV_LIBC_API
ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    return pwritev(fd, iov, iovcnt, -1);
}

TRACEPOINT(trace_vfs_ioctl, "%d 0x%x", int, unsigned long);
TRACEPOINT(trace_vfs_ioctl_ret, "");
TRACEPOINT(trace_vfs_ioctl_err, "%d", int);

OSV_LIBC_API
int ioctl(int fd, unsigned long int request, ...)
{
    struct file *fp;
    int error;
    va_list ap;
    void* arg;

    trace_vfs_ioctl(fd, request);
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
    trace_vfs_ioctl_ret();
    return 0;

    out_errno:
    trace_vfs_ioctl_err(error);
    errno = error;
    return -1;
}

TRACEPOINT(trace_vfs_fsync, "%d", int);
TRACEPOINT(trace_vfs_fsync_ret, "");
TRACEPOINT(trace_vfs_fsync_err, "%d", int);

OSV_LIBC_API
int fsync(int fd)
{
    struct file *fp;
    int error;

    trace_vfs_fsync(fd);
    error = fget(fd, &fp);
    if (error)
        goto out_errno;

    error = sys_fsync(fp);
    fdrop(fp);

    if (error)
        goto out_errno;
    trace_vfs_fsync_ret();
    return 0;

    out_errno:
    trace_vfs_fsync_err(error);
    errno = error;
    return -1;
}

OSV_LIBC_API
int fdatasync(int fd)
{
    // TODO: See if we can do less than fsync().
    return fsync(fd);
}

TRACEPOINT(trace_vfs_fstat, "%d %p", int, struct stat*);
TRACEPOINT(trace_vfs_fstat_ret, "");
TRACEPOINT(trace_vfs_fstat_err, "%d", int);

extern "C" OSV_LIBC_API
int __fxstat(int ver, int fd, struct stat *st)
{
    struct file *fp;
    int error;

    trace_vfs_fstat(fd, st);

    error = fget(fd, &fp);
    if (error)
        goto out_errno;

    error = sys_fstat(fp, st);
    fdrop(fp);

    if (error)
        goto out_errno;
    trace_vfs_fstat_ret();
    return 0;

    out_errno:
    trace_vfs_fstat_err(error);
    errno = error;
    return -1;
}

LFS64(__fxstat);

extern "C" OSV_LIBC_API
int fstat(int fd, struct stat *st)
{
    return __fxstat(1, fd, st);
}

extern "C" OSV_LIBC_API
int __fxstatat(int ver, int dirfd, const char *pathname, struct stat *st,
        int flags)
{
    if (!pathname) {
        errno = EINVAL;
        return -1;
    }

    if (pathname[0] == '/' || dirfd == AT_FDCWD) {
        if (flags & AT_SYMLINK_NOFOLLOW) {
            return lstat(pathname, st);
        }
        else {
            return stat(pathname, st);
        }
    }
    // If AT_EMPTY_PATH and pathname is an empty string, fstatat() operates on
    // dirfd itself, and in that case it doesn't have to be a directory.
    if ((flags & AT_EMPTY_PATH) && !pathname[0]) {
        return fstat(dirfd, st);
    }

    return vfs_fun_at(dirfd, pathname, [flags,st](const char *absolute_path) {
        if (flags & AT_SYMLINK_NOFOLLOW) {
            return lstat(absolute_path, st);
        }
        else {
            return stat(absolute_path, st);
        }
    });
}

LFS64(__fxstatat);

extern "C" OSV_LIBC_API
int fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
    return __fxstatat(1, dirfd, path, st, flags);
}

extern "C" OSV_LIBC_API
int flock(int fd, int operation)
{
    if (!fileref_from_fd(fd)) {
        return libc_error(EBADF);
    }

    switch (operation) {
    case LOCK_SH:
    case LOCK_SH | LOCK_NB:
    case LOCK_EX:
    case LOCK_EX | LOCK_NB:
    case LOCK_UN:
        break;
    default:
        return libc_error(EINVAL);
    }

    return 0;
}

TRACEPOINT(trace_vfs_readdir, "%d %p", int, dirent*);
TRACEPOINT(trace_vfs_readdir_ret, "");
TRACEPOINT(trace_vfs_readdir_err, "%d", int);

struct __dirstream
{
    int fd;
};

OSV_LIBC_API
DIR *opendir(const char *path)
{
    DIR *dir = new DIR;

    if (!dir)
        return libc_error_ptr<DIR>(ENOMEM);

    dir->fd = open(path, O_RDONLY);
    if (dir->fd < 0) {
        delete dir;
        return nullptr;
    }
    return dir;
}

OSV_LIBC_API
DIR *fdopendir(int fd)
{
    DIR *dir;
    struct stat st;
    if (fstat(fd, &st) < 0) {
        return nullptr;
    }
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return nullptr;
    }
    dir = new DIR;
    dir->fd = fd;
    return dir;

}

OSV_LIBC_API
int dirfd(DIR *dirp)
{
    if (!dirp) {
        return libc_error(EINVAL);
    }

    return dirp->fd;
}

OSV_LIBC_API
int closedir(DIR *dir)
{
    close(dir->fd);
    delete dir;
    return 0;
}

OSV_LIBC_API
struct dirent *readdir(DIR *dir)
{
    static __thread struct dirent entry, *result;
    int ret;

    ret = readdir_r(dir, &entry, &result);
    if (ret)
        return libc_error_ptr<struct dirent>(ret);

    errno = 0;
    return result;
}

OSV_LIBC_API
int readdir_r(DIR *dir, struct dirent *entry, struct dirent **result)
{
    int error;
    struct file *fp;

    trace_vfs_readdir(dir->fd, entry);
    error = fget(dir->fd, &fp);
    if (error) {
        trace_vfs_readdir_err(error);
    } else {
        error = sys_readdir(fp, entry);
        fdrop(fp);
        if (error) {
            trace_vfs_readdir_err(error);
        } else {
            trace_vfs_readdir_ret();
        }
    }
    // Our dirent has (like Linux) a d_reclen field, but a constant size.
    entry->d_reclen = sizeof(*entry);

    if (error) {
        *result = nullptr;
    } else {
        *result = entry;
    }
    return error == ENOENT ? 0 : error;
}

// FIXME: in 64bit dirent64 and dirent are identical, so it's safe to alias
#undef readdir64_r
extern "C" OSV_LIBC_API
int readdir64_r(DIR *dir, struct dirent64 *entry,
        struct dirent64 **result)
        __attribute__((alias("readdir_r")));

#undef readdir64
extern "C" OSV_LIBC_API
struct dirent *readdir64(DIR *dir) __attribute__((alias("readdir")));

struct linux_dirent64 {
    u64            d_ino;
    s64            d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
};

#undef getdents64
extern "C"
ssize_t sys_getdents64(int fd, void *dirp, size_t count)
{
    auto *dir = fdopendir(fd);
    if (dir) {
        // We have verified that fd points to a valid directory
        // but we do NOT need the DIR handle so just delete it
        delete dir;

        struct file *fp;
        int error = fget(fd, &fp);
        if (error) {
            errno = error;
            return -1;
        }

        size_t bytes_read = 0;
        off_t last_off = -1;
        errno = 0;

        // Iterate over as many entries as there is space in the buffer
        // by directly calling sys_readdir()
        struct dirent entry;
        while ((error = sys_readdir(fp, &entry)) == 0) {
            auto rec_len = offsetof(linux_dirent64, d_name) + strlen(entry.d_name) + 1;
            if (rec_len <= count) {
                auto *ldirent = static_cast<linux_dirent64*>(dirp + bytes_read);
                ldirent->d_ino = entry.d_ino;
                ldirent->d_off = entry.d_off;
                ldirent->d_type = entry.d_type;
                strcpy(ldirent->d_name, entry.d_name);
                ldirent->d_reclen = rec_len;
                count -= rec_len;
                bytes_read += rec_len;
                last_off = entry.d_off;
            } else {
                if (last_off >= 0)
                    sys_seekdir(fp, last_off);
                break;
            }
        }

        fdrop(fp);

        if (error && error != ENOENT) {
            errno = error;
            return -1;
        } else {
            errno = 0;
            return bytes_read;
        }
    } else {
        return -1;
    }
}

OSV_LIBC_API
void rewinddir(DIR *dirp)
{
    struct file *fp;

    auto error = fget(dirp->fd, &fp);
    if (error) {
        // POSIX specifies that what rewinddir() does in the case of error
        // is undefined...
        return;
    }

    sys_rewinddir(fp);
    // Again, error code from sys_rewinddir() is ignored.
    fdrop(fp);
}

OSV_LIBC_API
long telldir(DIR *dirp)
{
    struct file *fp;
    int error = fget(dirp->fd, &fp);
    if (error) {
        return libc_error(error);
    }

    long loc;
    error = sys_telldir(fp, &loc);
    fdrop(fp);
    if (error) {
        return libc_error(error);
    }
    return loc;
}

OSV_LIBC_API
void seekdir(DIR *dirp, long loc)
{
    struct file *fp;
    int error = fget(dirp->fd, &fp);
    if (error) {
        // POSIX specifies seekdir() cannot return errors.
        return;
    }
    sys_seekdir(fp, loc);
    // Again, error code from sys_seekdir() is ignored.
    fdrop(fp);
}

TRACEPOINT(trace_vfs_mkdir, "\"%s\" 0%0o", const char*, mode_t);
TRACEPOINT(trace_vfs_mkdir_ret, "");
TRACEPOINT(trace_vfs_mkdir_err, "%d", int);

OSV_LIBC_API
int
mkdir(const char *pathname, mode_t mode)
{
    struct task *t = main_task;
    char path[PATH_MAX];
    int error;

    mode = apply_umask(mode);

    trace_vfs_mkdir(pathname, mode);
    if ((error = task_conv(t, pathname, VWRITE, path)) != 0)
        goto out_errno;

    error = sys_mkdir(path, mode);
    if (error)
        goto out_errno;
    trace_vfs_mkdir_ret();
    return 0;
    out_errno:
    trace_vfs_mkdir_err(error);
    errno = error;
    return -1;
}

OSV_LIBC_API
int mkdirat(int dirfd, const char *pathname, mode_t mode)
{
    mode = apply_umask(mode);

    return vfs_fun_at2(dirfd, pathname, [mode](const char *path) {
        return mkdir(path, mode);
    });
}

TRACEPOINT(trace_vfs_rmdir, "\"%s\"", const char*);
TRACEPOINT(trace_vfs_rmdir_ret, "");
TRACEPOINT(trace_vfs_rmdir_err, "%d", int);

OSV_LIBC_API
int rmdir(const char *pathname)
{
    struct task *t = main_task;
    char path[PATH_MAX];
    int error;

    trace_vfs_rmdir(pathname);
    error = ENOENT;
    if (pathname == nullptr)
        goto out_errno;
    if ((error = task_conv(t, pathname, VWRITE, path)) != 0)
        goto out_errno;

    error = sys_rmdir(path);
    if (error)
        goto out_errno;
    trace_vfs_rmdir_ret();
    return 0;
    out_errno:
    trace_vfs_rmdir_err(error);
    errno = error;
    return -1;
}

static void
get_last_component(const char *path, char *dst)
{
    int pos = strlen(path) - 1;

    while (pos >= 0 && path[pos] == '/')
        pos--;

    int component_end = pos;

    while (pos >= 0 && path[pos] != '/')
        pos--;

    int component_start = pos + 1;

    int len = component_end - component_start + 1;
    memcpy(dst, path + component_start, len);
    dst[len] = 0;
}

static bool null_or_empty(const char *str)
{
    return str == nullptr || *str == '\0';
}

TRACEPOINT(trace_vfs_rename, "\"%s\" \"%s\"", const char*, const char*);
TRACEPOINT(trace_vfs_rename_ret, "");
TRACEPOINT(trace_vfs_rename_err, "%d", int);

OSV_LIBC_API
int rename(const char *oldpath, const char *newpath)
{
    trace_vfs_rename(oldpath, newpath);
    struct task *t = main_task;
    char src[PATH_MAX];
    char dest[PATH_MAX];
    int error;

    error = ENOENT;
    if (null_or_empty(oldpath) || null_or_empty(newpath))
        goto out_errno;

    get_last_component(oldpath, src);
    if (!strcmp(src, ".") || !strcmp(src, "..")) {
        error = EINVAL;
        goto out_errno;
    }

    get_last_component(newpath, dest);
    if (!strcmp(dest, ".") || !strcmp(dest, "..")) {
        error = EINVAL;
        goto out_errno;
    }

    if ((error = task_conv(t, oldpath, VREAD, src)) != 0)
        goto out_errno;

    if ((error = task_conv(t, newpath, VWRITE, dest)) != 0)
        goto out_errno;

    error = sys_rename(src, dest);
    if (error)
        goto out_errno;
    trace_vfs_rename_ret();
    return 0;
    out_errno:
    trace_vfs_rename_err(error);
    errno = error;
    return -1;
}

OSV_LIBC_API
int renameat(int olddirfd, const char *oldpath,
             int newdirfd, const char *newpath)
{
    if (!oldpath || !newpath) {
        errno = EINVAL;
        return -1;
    }

    if (newpath[0] == '/' || newdirfd == AT_FDCWD) {
        return vfs_fun_at2(olddirfd, oldpath, [newpath](const char *path) {
            return rename(path, newpath);
        });
    } else {
        char absolute_newpath[PATH_MAX];
        auto error = vfs_fun_at(newdirfd, newpath, [&absolute_newpath](const char *absolute_path) {
            strlcpy(absolute_newpath, absolute_path, PATH_MAX);
            return 0;
        });

        if (error) {
            return error;
        } else {
            return vfs_fun_at2(olddirfd, oldpath, [absolute_newpath](const char *path) {
                return rename(path, absolute_newpath);
            });
        }
    }
}

extern "C" OSV_LIBC_API
int renameat2(int olddirfd, const char *oldpath,
              int newdirfd, const char *newpath, unsigned int flags)
{
    if (flags) {
        errno = EINVAL;
        return -1;
    }
    return renameat(olddirfd, oldpath, newdirfd, newpath);
}

TRACEPOINT(trace_vfs_chdir, "\"%s\"", const char*);
TRACEPOINT(trace_vfs_chdir_ret, "");
TRACEPOINT(trace_vfs_chdir_err, "%d", int);

static int replace_cwd(struct task *t, struct file *new_cwdfp,
                       std::function<int (void)> chdir_func)
{
    struct file *old = nullptr;

    if (!t) {
        return 0;
    }

    if (t->t_cwdfp) {
        old = t->t_cwdfp;
    }

    /* Do the actual chdir operation here */
    int error = chdir_func();

    t->t_cwdfp = new_cwdfp;
    if (old) {
        fdrop(old);
    }

    return error;
}

OSV_LIBC_API
int chdir(const char *pathname)
{
    trace_vfs_chdir(pathname);
    struct task *t = main_task;
    char path[PATH_MAX];
    struct file *fp;
    int error;

    error = ENOENT;
    if (pathname == nullptr)
        goto out_errno;

    if ((error = task_conv(t, pathname, VREAD, path)) != 0)
        goto out_errno;

    /* Check if directory exits */
    error = sys_open(path, O_DIRECTORY, 0, &fp);
    if (error) {
        goto out_errno;
    }

    replace_cwd(t, fp, [&]() { strlcpy(t->t_cwd, path, sizeof(t->t_cwd)); return 0; });

    trace_vfs_chdir_ret();
    return 0;
    out_errno:
    errno = error;
    trace_vfs_chdir_err(errno);
    return -1;
}

TRACEPOINT(trace_vfs_fchdir, "%d", int);
TRACEPOINT(trace_vfs_fchdir_ret, "");
TRACEPOINT(trace_vfs_fchdir_err, "%d", int);

OSV_LIBC_API
int fchdir(int fd)
{
    trace_vfs_fchdir(fd);
    struct task *t = main_task;
    struct file *fp;
    int error;

    error = fget(fd, &fp);
    if (error)
        goto out_errno;

    error = replace_cwd(t, fp, [&]() { return sys_fchdir(fp, t->t_cwd); });
    if (error) {
        fdrop(fp);
        goto out_errno;
    }

    trace_vfs_fchdir_ret();
    return 0;

    out_errno:
    trace_vfs_fchdir_err(error);
    errno = error;
    return -1;
}

TRACEPOINT(trace_vfs_link, "\"%s\" \"%s\"", const char*, const char*);
TRACEPOINT(trace_vfs_link_ret, "");
TRACEPOINT(trace_vfs_link_err, "%d", int);

OSV_LIBC_API
int link(const char *oldpath, const char *newpath)
{
    struct task *t = main_task;
    char path1[PATH_MAX];
    char path2[PATH_MAX];
    int error;

    trace_vfs_link(oldpath, newpath);

    error = ENOENT;
    if (oldpath == nullptr || newpath == nullptr)
        goto out_errno;
    if ((error = task_conv(t, oldpath, VWRITE, path1)) != 0)
        goto out_errno;
    if ((error = task_conv(t, newpath, VWRITE, path2)) != 0)
        goto out_errno;

    error = sys_link(path1, path2);
    if (error)
        goto out_errno;
    trace_vfs_link_ret();
    return 0;
    out_errno:
    trace_vfs_link_err(error);
    errno = error;
    return -1;
}

OSV_LIBC_API
int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags)
{
    if (flags & AT_SYMLINK_FOLLOW) {
        WARN_ONCE("linkat() does not support AT_SYMLINK_FOLLOW\n");
        errno = EINVAL;
        return -1;
    }

    if (!oldpath || !newpath) {
        errno = EINVAL;
        return -1;
    }

    if (newpath[0] == '/' || newdirfd == AT_FDCWD) {
        return vfs_fun_at2(olddirfd, oldpath, [newpath](const char *path) {
            return link(path, newpath);
        });
    } else {
        char absolute_newpath[PATH_MAX];
        auto error = vfs_fun_at(newdirfd, newpath, [&absolute_newpath](const char *absolute_path) {
            strlcpy(absolute_newpath, absolute_path, PATH_MAX);
            return 0;
        });

        if (error) {
            return error;
        } else {
            return vfs_fun_at2(olddirfd, oldpath, [absolute_newpath](const char *path) {
                return link(path, absolute_newpath);
            });
        }
    }
}

TRACEPOINT(trace_vfs_symlink, "oldpath=%s, newpath=%s", const char*, const char*);
TRACEPOINT(trace_vfs_symlink_ret, "");
TRACEPOINT(trace_vfs_symlink_err, "errno=%d", int);

OSV_LIBC_API
int symlink(const char *oldpath, const char *newpath)
{
    int error;

    trace_vfs_symlink(oldpath, newpath);

    error = ENOENT;
    if (oldpath == nullptr || newpath == nullptr) {
        errno = ENOENT;
        trace_vfs_symlink_err(error);
        return (-1);
    }

    error = sys_symlink(oldpath, newpath);
    if (error) {
        errno = error;
        trace_vfs_symlink_err(error);
        return (-1);
    }

    trace_vfs_symlink_ret();
    return 0;
}

OSV_LIBC_API
int symlinkat(const char *oldpath, int newdirfd, const char *newpath)
{
    if (!oldpath) {
        errno = EINVAL;
        return -1;
    }

    return vfs_fun_at2(newdirfd, newpath, [oldpath](const char * path) {
        return symlink(oldpath, path);
    });
}

TRACEPOINT(trace_vfs_unlink, "\"%s\"", const char*);
TRACEPOINT(trace_vfs_unlink_ret, "");
TRACEPOINT(trace_vfs_unlink_err, "%d", int);

OSV_LIBC_API
int unlink(const char *pathname)
{
    trace_vfs_unlink(pathname);
    struct task *t = main_task;
    char path[PATH_MAX];
    int error;

    error = ENOENT;
    if (pathname == nullptr)
        goto out_errno;
    if ((error = task_conv(t, pathname, VWRITE, path)) != 0)
        goto out_errno;

    error = sys_unlink(path);
    if (error)
        goto out_errno;
    trace_vfs_unlink_ret();
    return 0;
    out_errno:
    trace_vfs_unlink_err(error);
    errno = error;
    return -1;
}

OSV_LIBC_API
int unlinkat(int dirfd, const char *pathname, int flags)
{
    return vfs_fun_at2(dirfd, pathname, [flags](const char *path) {
        if (flags & AT_REMOVEDIR) {
            return rmdir(path);
        } else {
            return unlink(path);
        }
    });
}

TRACEPOINT(trace_vfs_stat, "\"%s\" %p", const char*, struct stat*);
TRACEPOINT(trace_vfs_stat_ret, "");
TRACEPOINT(trace_vfs_stat_err, "%d", int);

extern "C" OSV_LIBC_API
int __xstat(int ver, const char *pathname, struct stat *st)
{
    struct task *t = main_task;
    char path[PATH_MAX];
    int error;

    trace_vfs_stat(pathname, st);

    error = task_conv(t, pathname, 0, path);
    if (error)
        goto out_errno;

    error = sys_stat(path, st);
    if (error)
        goto out_errno;
    trace_vfs_stat_ret();
    return 0;

    out_errno:
    trace_vfs_stat_err(error);
    errno = error;
    return -1;
}

LFS64(__xstat);

OSV_LIBC_API
int stat(const char *pathname, struct stat *st)
{
    return __xstat(1, pathname, st);
}

OSV_LIBC_API
int statx(int dirfd, const char* pathname, int flags, unsigned int mask,
    struct statx *buf)
{
    // This is a simplistic implementation which just calls the old fstatat()
    // and converts the resulting "struct stat" to a "struct statx".
    // This is good enough for OSv and its local filesystems, where we don't
    // expect great performance savings from not retrieving all the fields.
    struct stat st;
    int ret = fstatat(dirfd, pathname, &st, flags);
    if (ret != 0) {
        return ret;
    }
    // Convert the information we got in "struct stat" to "struct statx".
    // We are allowed to ignore the list of requested attributes in "mask" -
    // the documentation specifies that "the kernel may return fields that
    // weren't requested and may fail to return fields that were requested
    // ... Fields that are given values despite being unrequested can just
    // be ignored.". We just need to return stx_mask saying what we filled.
    buf->stx_mask = STATX_BASIC_STATS;
    buf->stx_blksize = st.st_blksize;
    buf->stx_attributes_mask = 0;
    buf->stx_nlink = st.st_nlink;
    buf->stx_uid = st.st_uid;
    buf->stx_gid = st.st_gid;
    buf->stx_mode = st.st_mode;
    buf->stx_ino = st.st_ino;
    buf->stx_size = st.st_size;
    buf->stx_blocks = st.st_blocks;
    buf->stx_atime.tv_sec = st.st_atim.tv_sec;
    buf->stx_atime.tv_nsec = st.st_atim.tv_nsec;
    buf->stx_mtime.tv_sec = st.st_mtim.tv_sec;
    buf->stx_mtime.tv_nsec = st.st_mtim.tv_nsec;
    buf->stx_ctime.tv_sec = st.st_ctim.tv_sec;
    buf->stx_ctime.tv_nsec = st.st_ctim.tv_nsec;
    buf->stx_rdev_major = major(st.st_rdev);
    buf->stx_rdev_minor = minor(st.st_rdev);
    buf->stx_dev_major = major(st.st_dev);
    buf->stx_dev_minor = minor(st.st_dev);
    return 0;
}

TRACEPOINT(trace_vfs_lstat, "pathname=%s, stat=%p", const char*, struct stat*);
TRACEPOINT(trace_vfs_lstat_ret, "");
TRACEPOINT(trace_vfs_lstat_err, "errno=%d", int);
extern "C" OSV_LIBC_API
int __lxstat(int ver, const char *pathname, struct stat *st)
{
    struct task *t = main_task;
    char path[PATH_MAX];
    int error;

    trace_vfs_lstat(pathname, st);

    error = task_conv(t, pathname, 0, path);
    if (error) {
        errno = error;
        trace_vfs_lstat_err(error);
        return (-1);
    }

    error = sys_lstat(path, st);
    if (error) {
        errno = error;
        trace_vfs_lstat_err(error);
        return (-1);
    }

    trace_vfs_lstat_ret();
    return 0;
}

LFS64(__lxstat);

OSV_LIBC_API
int lstat(const char *pathname, struct stat *st)
{
    return __lxstat(1, pathname, st);
}

TRACEPOINT(trace_vfs_statfs, "\"%s\" %p", const char*, struct statfs*);
TRACEPOINT(trace_vfs_statfs_ret, "");
TRACEPOINT(trace_vfs_statfs_err, "%d", int);

extern "C" OSV_LIBC_API
int __statfs(const char *pathname, struct statfs *buf)
{
    trace_vfs_statfs(pathname, buf);
    struct task *t = main_task;
    char path[PATH_MAX];
    int error;

    error = task_conv(t, pathname, 0, path);
    if (error)
        goto out_errno;

    error = sys_statfs(path, buf);
    if (error)
        goto out_errno;
    trace_vfs_statfs_ret();
    return 0;
    out_errno:
    trace_vfs_statfs_err(error);
    errno = error;
    return -1;
}
weak_alias(__statfs, statfs) OSV_LIBC_API;

TRACEPOINT(trace_vfs_fstatfs, "\"%s\" %p", int, struct statfs*);
TRACEPOINT(trace_vfs_fstatfs_ret, "");
TRACEPOINT(trace_vfs_fstatfs_err, "%d", int);

extern "C"
int __fstatfs(int fd, struct statfs *buf)
{
    struct file *fp;
    int error;

    trace_vfs_fstatfs(fd, buf);
    error = fget(fd, &fp);
    if (error)
        goto out_errno;

    error = sys_fstatfs(fp, buf);
    fdrop(fp);

    if (error)
        goto out_errno;
    trace_vfs_fstatfs_ret();
    return 0;

    out_errno:
    trace_vfs_fstatfs_err(error);
    errno = error;
    return -1;
}
weak_alias(__fstatfs, fstatfs) OSV_LIBC_API;

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

OSV_LIBC_API
int
statvfs(const char *pathname, struct statvfs *buf)
{
    struct statfs st;

    if (__statfs(pathname, &st) < 0)
        return -1;
    return statfs_to_statvfs(buf, &st);
}

OSV_LIBC_API
int
fstatvfs(int fd, struct statvfs *buf)
{
    struct statfs st;

    if (__fstatfs(fd, &st) < 0)
        return -1;
    return statfs_to_statvfs(buf, &st);
}

TRACEPOINT(trace_vfs_getcwd, "%p %d", char*, size_t);
TRACEPOINT(trace_vfs_getcwd_ret, "\"%s\"", const char*);
TRACEPOINT(trace_vfs_getcwd_err, "%d", int);

OSV_LIBC_API
char *getcwd(char *path, size_t size)
{
    trace_vfs_getcwd(path, size);
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
    trace_vfs_getcwd_ret(path);
    return path;

    out_errno:
    trace_vfs_getcwd_err(error);
    errno = error;
    return nullptr;
}

TRACEPOINT(trace_vfs_dup, "%d", int);
TRACEPOINT(trace_vfs_dup_ret, "%d", int);
TRACEPOINT(trace_vfs_dup_err, "%d", int);
/*
 * Duplicate a file descriptor
 */
OSV_LIBC_API
int dup(int oldfd)
{
    struct file *fp;
    int newfd;
    int error;

    trace_vfs_dup(oldfd);
    error = fget(oldfd, &fp);
    if (error)
        goto out_errno;

    error = fdalloc(fp, &newfd);
    if (error)
        goto out_fdrop;

    fdrop(fp);
    trace_vfs_dup_ret(newfd);
    return newfd;

    out_fdrop:
    fdrop(fp);
    out_errno:
    trace_vfs_dup_err(error);
    errno = error;
    return -1;
}

TRACEPOINT(trace_vfs_dup3, "%d %d 0x%x", int, int, int);
TRACEPOINT(trace_vfs_dup3_ret, "%d", int);
TRACEPOINT(trace_vfs_dup3_err, "%d", int);
/*
 * Duplicate a file descriptor to a particular value.
 */
OSV_LIBC_API
int dup3(int oldfd, int newfd, int flags)
{
    struct file *fp;
    int error;

    trace_vfs_dup3(oldfd, newfd, flags);
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
    trace_vfs_dup3_ret(newfd);
    return newfd;

    out_errno:
    trace_vfs_dup3_err(error);
    errno = error;
    return -1;
}

OSV_LIBC_API
int dup2(int oldfd, int newfd)
{
    if (oldfd == newfd)
        return newfd;

    return dup3(oldfd, newfd, 0);
}

/*
 * The file control system call.
 */
#define SETFL (O_APPEND | O_ASYNC | O_DIRECT | O_NOATIME | O_NONBLOCK)

TRACEPOINT(trace_vfs_fcntl, "%d %d 0x%x", int, int, int);
TRACEPOINT(trace_vfs_fcntl_ret, "%d", int);
TRACEPOINT(trace_vfs_fcntl_err, "%d", int);

extern "C" OSV_LIBC_API
int fcntl(int fd, int cmd, int arg)
{
    struct file *fp;
    int ret = 0, error;
    int tmp;

    trace_vfs_fcntl(fd, cmd, arg);
    error = fget(fd, &fp);
    if (error)
        goto out_errno;

    // An important note about our handling of FD_CLOEXEC / O_CLOEXEC:
    // close-on-exec shouldn't have been a file flag (fp->f_flags) - it is a
    // file descriptor flag, meaning that that two dup()ed file descriptors
    // could have different values for FD_CLOEXEC. Our current implementation
    // *wrongly* makes close-on-exec an f_flag (using the bit O_CLOEXEC).
    // There is little practical difference, though, because this flag is
    // ignored in OSv anyway, as it doesn't support exec().
    switch (cmd) {
    case F_DUPFD:
    // On Linux F_DUPFD_CLOEXEC is used to affect behavior of duplicated file descriptor
    // across execve() boundaries, but on OSv there is single process so we make it
    // behave exactly like F_DUPFD does
    case F_DUPFD_CLOEXEC:
        error = _fdalloc(fp, &ret, arg);
        if (error)
            goto out_errno;
        break;
    case F_GETFD:
        ret = (fp->f_flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
        break;
    case F_SETFD:
        FD_LOCK(fp);
        fp->f_flags = (fp->f_flags & ~O_CLOEXEC) |
                ((arg & FD_CLOEXEC) ? O_CLOEXEC : 0);
        FD_UNLOCK(fp);
        break;
    case F_GETFL:
        // As explained above, the O_CLOEXEC should have been in f_flags,
        // and shouldn't be returned. Linux always returns 0100000 ("the
        // flag formerly known as O_LARGEFILE) so let's do it too.
        ret = (oflags(fp->f_flags) & ~O_CLOEXEC) | 0100000;
        break;
    case F_SETFL:
        FD_LOCK(fp);
        fp->f_flags = fflags((oflags(fp->f_flags) & ~SETFL) |
                (arg & SETFL));
        FD_UNLOCK(fp);

        /* Sync nonblocking/async state with file flags */
        tmp = fp->f_flags & FNONBLOCK;
        fp->ioctl(FIONBIO, &tmp);
        tmp = fp->f_flags & FASYNC;
        fp->ioctl(FIOASYNC, &tmp);

        break;
    case F_SETLK:
        WARN_ONCE("fcntl(F_SETLK) stubbed\n");
        break;
    case F_GETLK:
        WARN_ONCE("fcntl(F_GETLK) stubbed\n");
        break;
    case F_SETLKW:
        WARN_ONCE("fcntl(F_SETLKW) stubbed\n");
        break;
    case F_SETOWN:
        WARN_ONCE("fcntl(F_SETOWN) stubbed\n");
        break;
    default:
        kprintf("unsupported fcntl cmd 0x%x\n", cmd);
        error = EINVAL;
    }

    fdrop(fp);
    if (error)
        goto out_errno;
    trace_vfs_fcntl_ret(ret);
    return ret;

    out_errno:
    trace_vfs_fcntl_err(error);
    errno = error;
    return -1;
}

LFS64(fcntl);

TRACEPOINT(trace_vfs_access, "\"%s\" 0%0o", const char*, int);
TRACEPOINT(trace_vfs_access_ret, "");
TRACEPOINT(trace_vfs_access_err, "%d", int);

/*
 * Check permission for file access
 */
OSV_LIBC_API
int access(const char *pathname, int mode)
{
    trace_vfs_access(pathname, mode);
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
    trace_vfs_access_ret();
    return 0;
    out_errno:
    errno = error;
    trace_vfs_access_err(error);
    return -1;
}

OSV_LIBC_API
int faccessat(int dirfd, const char *pathname, int mode, int flags)
{
    if (flags & AT_SYMLINK_NOFOLLOW) {
        UNIMPLEMENTED("faccessat() with AT_SYMLINK_NOFOLLOW");
    }

    return vfs_fun_at2(dirfd, pathname, [mode](const char *path) {
        return access(path, mode);
    });
}

extern "C" OSV_LIBC_API
int euidaccess(const char *pathname, int mode)
{
    return access(pathname, mode);
}

weak_alias(euidaccess,eaccess) OSV_LIBC_API;

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
        t->t_ofile[rfd] = nullptr;
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
    t->t_ofile[rfd] = nullptr;
    t->t_ofile[wfd] = nullptr;
    return error;
#else
    return ENOSYS;
#endif
}
#endif

TRACEPOINT(trace_vfs_isatty, "%d", int);
TRACEPOINT(trace_vfs_isatty_ret, "%d", int);
TRACEPOINT(trace_vfs_isatty_err, "%d", int);

/*
 * Return if specified file is a tty
 */
OSV_LIBC_API
int isatty(int fd)
{
    struct file *fp;
    int istty = 0;

    trace_vfs_isatty(fd);
    fileref f(fileref_from_fd(fd));
    if (!f) {
        errno = EBADF;
        trace_vfs_isatty_err(errno);
        return -1;
    }

    fp = f.get();
    if (dynamic_cast<tty_file*>(fp) ||
        (fp->f_dentry && fp->f_dentry->d_vnode->v_flags & VISTTY)) {
        istty = 1;
    }

    trace_vfs_isatty_ret(istty);
    return istty;
}

TRACEPOINT(trace_vfs_truncate, "\"%s\" 0x%x", const char*, off_t);
TRACEPOINT(trace_vfs_truncate_ret, "");
TRACEPOINT(trace_vfs_truncate_err, "%d", int);

OSV_LIBC_API
int truncate(const char *pathname, off_t length)
{
    trace_vfs_truncate(pathname, length);
    struct task *t = main_task;
    char path[PATH_MAX];
    int error;

    error = ENOENT;
    if (pathname == nullptr)
        goto out_errno;
    if ((error = task_conv(t, pathname, VWRITE, path)) != 0)
        goto out_errno;

    error = sys_truncate(path, length);
    if (error)
        goto out_errno;
    trace_vfs_truncate_ret();
    return 0;
    out_errno:
    errno = error;
    trace_vfs_truncate_err(error);
    return -1;
}

LFS64(truncate);

TRACEPOINT(trace_vfs_ftruncate, "%d 0x%x", int, off_t);
TRACEPOINT(trace_vfs_ftruncate_ret, "");
TRACEPOINT(trace_vfs_ftruncate_err, "%d", int);

OSV_LIBC_API
int ftruncate(int fd, off_t length)
{
    trace_vfs_ftruncate(fd, length);
    struct file *fp;
    int error;

    error = fget(fd, &fp);
    if (error)
        goto out_errno;

    error = sys_ftruncate(fp, length);
    fdrop(fp);

    if (error)
        goto out_errno;
    trace_vfs_ftruncate_ret();
    return 0;

    out_errno:
    errno = error;
    trace_vfs_ftruncate_err(error);
    return -1;
}

LFS64(ftruncate);

OSV_LIBC_API
ssize_t readlink(const char *pathname, char *buf, size_t bufsize)
{
    struct task *t = main_task;
    char path[PATH_MAX];
    int error;
    ssize_t size;

    error = -EINVAL;
    if (bufsize <= 0)
        goto out_errno;

    error = ENOENT;
    if (pathname == nullptr)
        goto out_errno;
    error = task_conv(t, pathname, VWRITE, path);
    if (error)
        goto out_errno;

    size  = 0;
    error = sys_readlink(path, buf, bufsize, &size);

    if (error != 0)
        goto out_errno;

    return size;
    out_errno:
    errno = error;
    return -1;
}

OSV_LIBC_API
ssize_t readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsize)
{
    return vfs_fun_at2(dirfd, pathname, [buf, bufsize](const char *path) {
        return readlink(path, buf, bufsize);
    });
}

TRACEPOINT(trace_vfs_fallocate, "%d %d 0x%x 0x%x", int, int, loff_t, loff_t);
TRACEPOINT(trace_vfs_fallocate_ret, "");
TRACEPOINT(trace_vfs_fallocate_err, "%d", int);

OSV_LIBC_API
int fallocate(int fd, int mode, loff_t offset, loff_t len)
{
    struct file *fp;
    int error;

    trace_vfs_fallocate(fd, mode, offset, len);
    error = fget(fd, &fp);
    if (error)
        goto out_errno;

    error = sys_fallocate(fp, mode, offset, len);
    fdrop(fp);

    if (error)
        goto out_errno;
    trace_vfs_fallocate_ret();
    return 0;

    out_errno:
    trace_vfs_fallocate_err(error);
    errno = error;
    return -1;
}

LFS64(fallocate);

TRACEPOINT(trace_vfs_utimes, "\"%s\"", const char*);
TRACEPOINT(trace_vfs_utimes_ret, "");
TRACEPOINT(trace_vfs_utimes_err, "%d", int);

OSV_LIBC_API
int futimes(int fd, const struct timeval times[2])
{
    return futimesat(fd, nullptr, times);
}

OSV_LIBC_API
int futimesat(int dirfd, const char *pathname, const struct timeval times[2])
{
    struct stat st;
    int error;

    if ((pathname && pathname[0] == '/') || dirfd == AT_FDCWD)
        return utimes(pathname, times);

    // Note: if pathname == nullptr, futimesat operates on dirfd itself, and in
    // that case it doesn't have to be a directory.
    if (pathname) {
        error = fstat(dirfd, &st);
        if (error) {
            error = errno;
            goto out_errno;
        }

        if (!S_ISDIR(st.st_mode)){
            error = ENOTDIR;
            goto out_errno;
        }
    }

    error = vfs_fun_at(dirfd, pathname, [times](const char *absolute_path) {
        return utimes(absolute_path, times);
    });

    if (error)
        goto out_errno;
    return 0;

    out_errno:
    errno = error;
    return -1;
}

TRACEPOINT(trace_vfs_utimensat, "\"%s\"", const char*);
TRACEPOINT(trace_vfs_utimensat_ret, "");
TRACEPOINT(trace_vfs_utimensat_err, "%d", int);

extern "C" OSV_LIBC_API
int utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags)
{
    trace_vfs_utimensat(pathname);

    auto error = sys_utimensat(dirfd, pathname, times, flags, false);
    if (error) {
        trace_vfs_utimensat_err(error);
        errno = error;
        return -1;
    }

    trace_vfs_utimensat_ret();
    return 0;
}

int utimensat4(int dirfd, const char *pathname, const struct timespec times[2], int flags)
{
    trace_vfs_utimensat(pathname);

    auto error = sys_utimensat(dirfd, pathname, times, flags, true);
    if (error) {
        trace_vfs_utimensat_err(error);
        errno = error;
        return -1;
    }

    trace_vfs_utimensat_ret();
    return 0;
}

TRACEPOINT(trace_vfs_futimens, "%d", int);
TRACEPOINT(trace_vfs_futimens_ret, "");
TRACEPOINT(trace_vfs_futimens_err, "%d", int);

extern "C" OSV_LIBC_API
int futimens(int fd, const struct timespec times[2])
{
    trace_vfs_futimens(fd);

    auto error = sys_futimens(fd, times);
    if (error) {
        trace_vfs_futimens_err(error);
        errno = error;
        return -1;
    }

    trace_vfs_futimens_ret();
    return 0;
}

static int do_utimes(const char *pathname, const struct timeval times[2], int flags)
{
    struct task *t = main_task;
    char path[PATH_MAX];
    int error;

    trace_vfs_utimes(pathname);

    error = task_conv(t, pathname, 0, path);
    if (error) {
        trace_vfs_utimes_err(error);
        return libc_error(error);
    }

    error = sys_utimes(path, times, flags);
    if (error) {
        trace_vfs_utimes_err(error);
        return libc_error(error);
    }

    trace_vfs_utimes_ret();
    return 0;
}

extern "C" OSV_LIBC_API
int utimes(const char *pathname, const struct timeval times[2])
{
    return do_utimes(pathname, times, 0);
}

extern "C" OSV_LIBC_API
int lutimes(const char *pathname, const struct timeval times[2])
{
    return do_utimes(pathname, times, AT_SYMLINK_NOFOLLOW);
}

extern "C" OSV_LIBC_API
int utime(const char *pathname, const struct utimbuf *t)
{
    using namespace std::chrono;

    struct timeval times[2];
    times[0].tv_usec = 0;
    times[1].tv_usec = 0;
    if (!t) {
        long int tsec = duration_cast<seconds>(osv::clock::wall::now().time_since_epoch()).count();
        times[0].tv_sec = tsec;
        times[1].tv_sec = tsec;
    } else {
        times[0].tv_sec = t->actime;
        times[1].tv_sec = t->modtime;
    }

    return utimes(pathname, times);
}

TRACEPOINT(trace_vfs_chmod, "\"%s\" 0%0o", const char*, mode_t);
TRACEPOINT(trace_vfs_chmod_ret, "");
TRACEPOINT(trace_vfs_chmod_err, "%d", int);

OSV_LIBC_API
int chmod(const char *pathname, mode_t mode)
{
    trace_vfs_chmod(pathname, mode);
    struct task *t = main_task;
    char path[PATH_MAX];
    int error = ENOENT;
    if (pathname == nullptr)
        goto out_errno;
    if ((error = task_conv(t, pathname, VWRITE, path)) != 0)
        goto out_errno;
    error = sys_chmod(path, mode & ALLPERMS);
    if (error)
        goto out_errno;
    trace_vfs_chmod_ret();
    return 0;
out_errno:
    trace_vfs_chmod_err(error);
    errno = error;
    return -1;
}

TRACEPOINT(trace_vfs_fchmod, "\"%d\" 0%0o", int, mode_t);
TRACEPOINT(trace_vfs_fchmod_ret, "");

OSV_LIBC_API
int fchmod(int fd, mode_t mode)
{
    trace_vfs_fchmod(fd, mode);
    auto error = sys_fchmod(fd, mode & ALLPERMS);
    trace_vfs_fchmod_ret();
    if (error) {
        errno = error;
        return -1;
    } else {
        return 0;
    }
}

TRACEPOINT(trace_vfs_fchown, "\"%d\" %d %d", int, uid_t, gid_t);
TRACEPOINT(trace_vfs_fchown_ret, "");

OSV_LIBC_API
int fchown(int fd, uid_t owner, gid_t group)
{
    trace_vfs_fchown(fd, owner, group);
    WARN_STUBBED();
    trace_vfs_fchown_ret();
    return 0;
}

OSV_LIBC_API
int chown(const char *path, uid_t owner, gid_t group)
{
    WARN_STUBBED();
    return 0;
}

OSV_LIBC_API
int lchown(const char *path, uid_t owner, gid_t group)
{
    WARN_STUBBED();
    return 0;
}


OSV_LIBC_API
ssize_t sendfile(int out_fd, int in_fd, off_t *_offset, size_t count)
{
    struct file *in_fp;
    struct file *out_fp;
    fileref in_f{fileref_from_fd(in_fd)};
    fileref out_f{fileref_from_fd(out_fd)};

    if (!in_f || !out_f) {
        return libc_error(EBADF);
    }

    in_fp = in_f.get();
    out_fp = out_f.get();

    if (!in_fp->f_dentry) {
        return libc_error(EBADF);
    }

    if (!(in_fp->f_flags & FREAD)) {
        return libc_error(EBADF);
    }

    if (out_fp->f_type & DTYPE_VNODE) {
        if (!out_fp->f_dentry) {
            return libc_error(EBADF);
	} else if (!(out_fp->f_flags & FWRITE)) {
            return libc_error(EBADF);
	}
    }

    off_t offset ;

    if (_offset != nullptr) {
        offset = *_offset;
    } else {
        /* if _offset is nullptr, we need to read from the present position of in_fd */
        offset = lseek(in_fd, 0, SEEK_CUR);
    }

    // Constrain count to the extent of the file...
    struct stat st;
    if (fstat(in_fd, &st) < 0) {
        return -1;
    } else {
        if (offset >= st.st_size) {
            return 0;
        } else if ((offset + count) >= st.st_size) {
            count = st.st_size - offset;
            if (count == 0) {
                return 0;
            }
        }
    }

    size_t bytes_to_mmap = count + (offset % mmu::page_size);
    off_t offset_for_mmap =  align_down(offset, (off_t)mmu::page_size);

    char *src = static_cast<char *>(mmap(nullptr, bytes_to_mmap, PROT_READ, MAP_SHARED, in_fd, offset_for_mmap));

    if (src == MAP_FAILED) {
        return -1;
    }

    auto ret = write(out_fd, src + (offset % PAGE_SIZE), count);

    if (ret < 0) {
        return libc_error(errno);
    } else if(_offset == nullptr) {
        lseek(in_fd, ret, SEEK_CUR);
    } else {
        *_offset += ret;
    }

    assert(munmap(src, count) == 0);

    return ret;
}

#undef sendfile64
LFS64(sendfile);

extern "C" OSV_LIBC_API
ssize_t copy_file_range(int fd_in, off_t *off_in,
                        int fd_out, off_t *off_out,
                        size_t len, unsigned int flags)
{
    //Non-zero flags are rejected according to the manual
    if (flags != 0) {
        errno = EINVAL;
        return -1;
    }
    //We do not support writing to a file at specified offset because
    //we delegate to sendfile() which assumes current position of the output
    //file
    if (off_out) {
        WARN("copy_file_range() does not support non-zero off_out\n");
        errno = EINVAL;
        return -1;
    }
    return sendfile(fd_out, fd_in, off_in, len);
}

NO_SYS(OSV_LIBC_API int fchmodat(int dirfd, const char *pathname, mode_t mode, int flags));

OSV_LIBC_API
mode_t umask(mode_t newmask)
{
    return global_umask.exchange(newmask, std::memory_order_relaxed);
}

int
fs_noop(void)
{
    return 0;
}

OSV_LIBC_API
int chroot(const char *path)
{
    WARN_STUBBED();
    errno = ENOSYS;
    return -1;
}

// unpack_bootfs() unpacks a collection of files stored as part of the OSv
// executable (in memory location "bootfs_start") into the file system,
// normally the in-memory filesystem ramfs.
// The files are packed in the executable in an ad-hoc format defined here.
// Code in scripts/mkbootfs.py packs files into this format.
#define BOOTFS_PATH_MAX 111
enum class bootfs_file_type : char { other = 0, symlink = 1 };
struct bootfs_metadata {
    uint64_t size;
    uint64_t offset;
    // The file's type. Can be "symlink" or "other". A directory is an "other"
    // file with its name ending with a "/" (and no content).
    bootfs_file_type type;
    // name must end with a null. For symlink files, the content must end
    // with a null as well.
    char name[BOOTFS_PATH_MAX];
};

extern char bootfs_start[];

int ramfs_set_file_data(struct vnode *vp, const void *data, size_t size);
void unpack_bootfs(void)
{
    struct bootfs_metadata *md = (struct bootfs_metadata *)&bootfs_start;
    int fd, i;

    for (i = 0; md[i].name[0]; i++) {
        int ret;
        char *p;

        // mkdir() directories needed for this path name, as necessary
        char tmp[BOOTFS_PATH_MAX];
        strlcpy(tmp, md[i].name, BOOTFS_PATH_MAX);
        for (p = tmp; *p; ++p) {
            if (*p == '/') {
                *p = '\0';
                mkdir(tmp, 0666);  // silently ignore errors and existing dirs
                *p = '/';
            }
        }

        if (md[i].type == bootfs_file_type::symlink) {
            // This is a symbolic link record. The file's content is the
            // target path, and we assume ends with a null.
            if (symlink(&bootfs_start[md[i].offset], md[i].name) != 0) {
                kprintf("couldn't symlink %s: %d\n", md[i].name, errno);
                sys_panic("unpack_bootfs failed");
            }
            continue;
        }
        if (*(p-1) == '/' && md[i].size == 0) {
            // This is directory record. Nothing else to do
            continue;
        }

        fd = creat(md[i].name, 0666);
        if (fd < 0) {
            kprintf("couldn't create %s: %d\n",
                    md[i].name, errno);
            sys_panic("unpack_bootfs failed");
        }

        struct file *fp;
        int error = fget(fd, &fp);
        if (error) {
            kprintf("couldn't fget %s: %d\n",
                    md[i].name, error);
            sys_panic("unpack_bootfs failed");
        }

        struct vnode *vp = fp->f_dentry->d_vnode;
        ret = ramfs_set_file_data(vp, &bootfs_start[md[i].offset], md[i].size);
        if (ret) {
            kprintf("ramfs_set_file_data failed, ret = %d\n", ret);
            sys_panic("unpack_bootfs failed");
        }

        fdrop(fp);
        close(fd);
    }
}

void mount_rootfs(void)
{
    int ret;

    ret = sys_mount("", "/", "ramfs", 0, nullptr);
    if (ret)
        kprintf("failed to mount rootfs, error = %s\n", strerror(ret));

    if (mkdir("/dev", 0755) < 0)
        kprintf("failed to create /dev, error = %s\n", strerror(errno));

    ret = sys_mount("", "/dev", "devfs", 0, nullptr);
    if (ret)
        kprintf("failed to mount devfs, error = %s\n", strerror(ret));
}

extern "C" OSV_LIBSOLARIS_API
int nmount(struct iovec *iov, unsigned niov, int flags)
{
    struct args {
        char* fstype = nullptr;
        char* fspath = nullptr;
        char* from = nullptr;
    };
    static unordered_map<string, char* args::*> argmap {
        { "fstype", &args::fstype },
        { "fspath", &args::fspath },
        { "from", &args::from },
    };
    args a;
    for (size_t i = 0; i < niov; i += 2) {
        std::string s(static_cast<const char*>(iov[i].iov_base));
        if (argmap.count(s)) {
            a.*(argmap[s]) = static_cast<char*>(iov[i+1].iov_base);
        }
    }
    return sys_mount(a.from, a.fspath, a.fstype, flags, nullptr);
}

static void import_extra_zfs_pools(void)
{
    struct stat st;
    int ret;

    // The file '/etc/mnttab' is a LibZFS requirement and will not
    // exist during cpiod phase. The functionality provided by this
    // function isn't needed during that phase, so let's skip it.
    if (stat("/etc/mnttab" , &st) != 0) {
        return;
    }

    // Import extra pools mounting datasets there contained.
    // Datasets from osv pool will not be mounted here.
    if (access("zpool.so", X_OK) != 0) {
        return;
    }
    vector<string> zpool_args = {"zpool", "import", "-f", "-a" };
    auto ok = osv::run("zpool.so", zpool_args, &ret);
    assert(ok);

    if (!ret) {
        debug("zfs: extra ZFS pool(s) found.\n");
    }
}

static void mount_fs(mntent *m)
{
    if (!strcmp(m->mnt_dir, "/")) {
        return;
    }

    bool zfs = strcmp(m->mnt_type, "zfs") == 0;
    if (zfs) {
        // Ignore if ZFS root pool is already mounted because we can only have one root pool
        std::vector<osv::mount_desc> mounts = osv::current_mounts();
        for (auto &mount : mounts) {
            if (mount.type == "zfs" && mount.special.rfind("/dev")) {
                kprintf("ZFS root pool is already mounted at %s\n", m->mnt_dir);
                return;
            }
        }
    }

    auto mount_dir = opendir(m->mnt_dir);
    if (!mount_dir) {
        if (mkdir(m->mnt_dir, 0755) < 0) {
            kprintf("failed to create missing mount point %s, error = %s\n", m->mnt_dir, strerror(errno));
            return;
        } else {
            kprintf("created missing mount point %s\n", m->mnt_dir);
        }
    } else {
        closedir(mount_dir);
    }

    if (zfs) {
        m->mnt_opts = "osv/zfs";
    } else {
        if ((m->mnt_opts != nullptr) && strcmp(m->mnt_opts, MNTOPT_DEFAULTS)) {
            printf("Warning: opts %s, ignored for fs %s\n", m->mnt_opts, m->mnt_type);
        }
        m->mnt_opts = nullptr;
    }

    // FIXME: Right now, ignoring mntops except for ZFS. In the future we may have an option parser
    auto ret = sys_mount(m->mnt_fsname, m->mnt_dir, m->mnt_type, 0, (void*)m->mnt_opts);
    if (ret) {
        printf("failed to mount %s, error = %s\n", m->mnt_type, strerror(ret));
    } else {
        if (zfs) {
            bsd_shrinker_init();
        }
    }
}

extern std::vector<mntent> opt_mount_fs;
extern "C" void pivot_rootfs(const char* path)
{
    int ret = sys_pivot_root(path, "/");
    if (ret)
        kprintf("failed to pivot root, error = %s\n", strerror(ret));

    // Initialize other filesystem libraries if present
    auto fs_lib_dir = opendir("/usr/lib/fs");
    if (fs_lib_dir) {
        while (auto dirent = readdir(fs_lib_dir)) {
            auto len = strlen(dirent->d_name);
            if (len >= 3 && strcmp(dirent->d_name + (len - 3), ".so") == 0) {
                auto lib_path = std::string("/usr/lib/fs/") + dirent->d_name;
                auto module = dlopen(lib_path.c_str(), RTLD_LAZY);
                if (module) {
                    if (strcmp(dirent->d_name, "libsolaris.so") == 0) {
                        zfsdev::zfsdev_init();
                    }
                    debugf("VFS: initialized filesystem library: %s\n", lib_path.c_str());
                }
            }
        }

        closedir(fs_lib_dir);
    }

    auto ent = setmntent("/etc/fstab", "r");
    if (ent) {
        struct mntent *m = nullptr;
        while ((m = getmntent(ent)) != nullptr) {
            mount_fs(m);
        }
        endmntent(ent);
    }

    for (auto m: opt_mount_fs) {
        mount_fs(&m);
    }
}

extern "C" void unmount_devfs()
{
    int ret = sys_umount("/dev");
    if (ret)
        kprintf("failed to unmount /dev, error = %s\n", strerror(ret));
}

extern "C" int mount_rofs_rootfs(bool pivot_root)
{
    constexpr char* mp = "/rofs";

    if (mkdir(mp, 0755) < 0) {
        int ret = errno;
        kprintf("failed to create %s, error = %s\n", mp, strerror(errno));
        return ret;
    }

    int ret = sys_mount("/dev/vblk0.1", mp, "rofs", MNT_RDONLY, nullptr);
    if (ret) {
        kprintf("failed to mount %s, error = %s\n", mp, strerror(ret));
        rmdir(mp);
        return ret;
    }

    if (pivot_root) {
        pivot_rootfs(mp);
    }

    return 0;
}

extern "C" int mount_zfs_rootfs(bool pivot_root, bool extra_zfs_pools)
{
    constexpr char* mp = "/zfs";

    if (mkdir(mp, 0755) < 0) {
        int ret = errno;
        kprintf("failed to create %s, error = %s\n", mp, strerror(errno));
        return ret;
    }

    int ret = sys_mount("/dev/vblk0.1", mp, "zfs", 0, (void *)"osv/zfs");
    if (ret) {
        kprintf("failed to mount %s, error = %s\n", mp, strerror(ret));
        rmdir(mp);
        return ret;
    }

    if (pivot_root) {
        pivot_rootfs(mp);
        if (extra_zfs_pools) {
            import_extra_zfs_pools();
        }
    }

    return 0;
}

extern "C" int mount_virtiofs_rootfs(bool pivot_root)
{
    constexpr char* mp = "/virtiofs";

    if (mkdir(mp, 0755) < 0) {
        int ret = errno;
        kprintf("failed to create %s, error = %s\n", mp, strerror(errno));
        return ret;
    }

    int ret = sys_mount("/dev/virtiofs0", mp, "virtiofs", MNT_RDONLY, nullptr);
    if (ret) {
        kprintf("failed to mount %s, error = %s\n", mp, strerror(ret));
        rmdir(mp);
        return ret;
    }

    if (pivot_root) {
        pivot_rootfs(mp);
    }

    return 0;
}

extern "C" void unmount_rootfs(void)
{
    int ret;

    sys_umount("/dev");

    ret = sys_umount("/proc");
    if (ret) {
        kprintf("Warning: unmount_rootfs: failed to unmount /proc, "
            "error = %s\n", strerror(ret));
    }

    std::vector<osv::mount_desc> mounts = osv::current_mounts();
    for (auto &m : mounts) {
        if (m.type == "zfs" && m.special.rfind("/dev") == 0 && m.path != "/") {
            ret = sys_umount2(m.path.c_str(), MNT_FORCE);
            if (ret) {
                kprintf("Warning: unmount_rootfs: failed to unmount %s, "
                    "error = %s\n", m.path.c_str(), strerror(ret));
            }
        }
    }

    ret = sys_umount2("/", MNT_FORCE);
    if (ret) {
        kprintf("Warning: unmount_rootfs: failed to unmount /, "
            "error = %s\n", strerror(ret));
    }
}

extern "C" void bio_init(void);
extern "C" void bio_sync(void);

int vfs_initialized;

extern "C"
void
vfs_init(void)
{
    const struct vfssw *fs;

    bio_init();
    lookup_init();
    vnode_init();
    task_alloc(&main_task);

    /*
     * Initialize each file system.
     */
    for (fs = vfssw; fs->vs_name; fs++) {
        if (fs->vs_init) {
            DPRINTF(VFSDB_CORE, ("VFS: initializing %s\n",
                    fs->vs_name));
            fs->vs_init();
        }
    }

    mount_rootfs();
    unpack_bootfs();

    //	if (open("/dev/console", O_RDWR, 0) != 0)
    if (console::open() != 0)
        kprintf("failed to open console, error = %d\n", errno);
    if (dup(0) != 1)
        kprintf("failed to dup console (1)\n");
    if (dup(0) != 2)
        kprintf("failed to dup console (2)\n");
    vfs_initialized = 1;
}

void vfs_exit(void)
{
    /* Free up main_task (stores cwd data) resources */
    replace_cwd(main_task, nullptr, []() { return 0; });
    /* Unmount file systems mounted with '--mount-fs=...' boot option */
    for (auto m: opt_mount_fs) {
        sys_umount(m.mnt_dir);
    }
    /* Unmount all file systems */
    unmount_rootfs();
    /* Finish with the bio layer */
    bio_sync();
}

void sys_panic(const char *str)
{
    abort("panic: %s", str);
}

