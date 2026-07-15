/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// splice(2), vmsplice(2), tee(2).
//
// Linux implements these as zero-copy transfers through a pipe's page buffers.
// OSv has no user/kernel boundary and the copies are cheap, so this is a
// correct (not zero-copy) implementation built on the existing read/write and
// pread/pwrite paths: move up to `len` bytes through a bounce buffer.  This
// unblocks the many programs that use splice()/sendfile-style loops without
// caring how the bytes move.
//
// TODO: this uses a bounce buffer rather than sharing pipe pages zero-copy.
// If splice() ever shows up as a hot path, a pipe-page-sharing fast path could
// be added; correctness does not need it.
//
// Limitation: Linux requires exactly one of splice()'s ends to be a pipe and
// forbids a non-null offset on the pipe end.  fd_in/fd_out may be any fd the
// read/write path accepts (filesystem files, pipes, sockets).  OSv's anonymous
// pipe is not reliably distinguishable via fstat here, so we do NOT enforce the
// "exactly one end is a pipe" rule; we DO reject a non-null offset on a
// non-seekable fd (ESPIPE), which is the observable part of that rule.

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/uio.h>
#include <algorithm>
#include <vector>

#include <osv/export.h>
#include <libc/libc.hh>

// A splice moves at most this many bytes per internal chunk.
static const size_t SPLICE_CHUNK = 64 * 1024;

// Read up to n bytes from fd (at *off if non-null, advancing it; else at the
// current position).  Returns bytes read, 0 on EOF, or -1 with errno.
static ssize_t read_at(int fd, void *buf, size_t n, off_t *off)
{
    if (off) {
        ssize_t r = pread(fd, buf, n, *off);
        if (r > 0) {
            *off += r;
        }
        return r;
    }
    return read(fd, buf, n);
}

static ssize_t write_at(int fd, const void *buf, size_t n, off_t *off)
{
    if (off) {
        ssize_t w = pwrite(fd, buf, n, *off);
        if (w > 0) {
            *off += w;
        }
        return w;
    }
    return write(fd, buf, n);
}

extern "C" OSV_LIBC_API
ssize_t splice(int fd_in, off_t *off_in, int fd_out, off_t *off_out,
               size_t len, unsigned int flags)
{
    if (flags & ~(SPLICE_F_MOVE | SPLICE_F_NONBLOCK | SPLICE_F_MORE | SPLICE_F_GIFT)) {
        return libc_error(EINVAL);
    }
    if (len == 0) {
        return 0;
    }
    // The return value is ssize_t; reject a length that could not be
    // represented (and would make the total wrap negative).
    if (len > SSIZE_MAX) {
        return libc_error(EINVAL);
    }
    // Linux forbids a non-null offset on a pipe (or any non-seekable) fd; a
    // non-seekable fd reports ESPIPE from lseek.  Reject that here (matching
    // Linux's ESPIPE) rather than silently pread/pwrite'ing at a bogus offset.
    if (off_in && lseek(fd_in, 0, SEEK_CUR) < 0 && errno == ESPIPE) {
        return libc_error(ESPIPE);
    }
    if (off_out && lseek(fd_out, 0, SEEK_CUR) < 0 && errno == ESPIPE) {
        return libc_error(ESPIPE);
    }

    std::vector<char> buf(std::min(len, SPLICE_CHUNK));
    size_t total = 0;
    while (total < len) {
        size_t want = std::min(len - total, buf.size());
        ssize_t r = read_at(fd_in, buf.data(), want, off_in);
        if (r < 0) {
            return total ? (ssize_t)total : -1;   // errno set by read
        }
        if (r == 0) {
            break;   // EOF on the source
        }
        // Write out everything we read (short writes to a pipe/socket are
        // possible; loop until the chunk is drained).
        ssize_t written = 0;
        while (written < r) {
            ssize_t w = write_at(fd_out, buf.data() + written, r - written, off_out);
            if (w < 0) {
                return total + written ? (ssize_t)(total + written) : -1;
            }
            if (w == 0) {
                // No forward progress (a legal short write of 0); stop rather
                // than spin forever.  Report what we moved.
                return (ssize_t)(total + written);
            }
            written += w;
        }
        total += r;
        if ((size_t)r < want) {
            break;   // source had less than we asked; done
        }
    }
    return total;
}

extern "C" OSV_LIBC_API
ssize_t vmsplice(int fd, const struct iovec *iov, size_t nr_segs, unsigned int flags)
{
    if (flags & ~(SPLICE_F_MOVE | SPLICE_F_NONBLOCK | SPLICE_F_MORE | SPLICE_F_GIFT)) {
        return libc_error(EINVAL);
    }
    // vmsplice moves memory <-> a pipe.  Without zero-copy pipe buffers we
    // implement only the memory -> pipe/fd direction (a plain write of the
    // iovec into the fd), which is the common use.  SPLICE_F_GIFT (the caller
    // gifting its pages to the kernel) is a no-op hint for us: there are no
    // shared pipe pages to take ownership of, so we simply copy.  The
    // unsupported pipe -> memory direction is rejected naturally: an fd opened
    // only for reading (e.g. a pipe read end) makes write() below fail with
    // EBADF, so we do not need an explicit direction check.
    // Bound nr_segs like the readv/writev path (UIO_MAXIOV) so a bogus count
    // cannot walk iov[] out of bounds, and guard the running total against
    // ssize_t overflow across large iovecs.
    if (nr_segs > UIO_MAXIOV) {
        return libc_error(EINVAL);
    }
    size_t total = 0;
    for (size_t i = 0; i < nr_segs; i++) {
        if (iov[i].iov_len > (size_t)SSIZE_MAX - total) {
            return libc_error(EINVAL);
        }
        total += iov[i].iov_len;
    }

    ssize_t moved = 0;
    for (size_t i = 0; i < nr_segs; i++) {
        const char *p = static_cast<const char *>(iov[i].iov_base);
        size_t left = iov[i].iov_len;
        while (left) {
            ssize_t w = write(fd, p, left);
            if (w < 0) {
                return moved ? moved : -1;
            }
            if (w == 0) {
                return moved;   // no progress; stop
            }
            p += w;
            left -= w;
            moved += w;
        }
    }
    return moved;
}

extern "C" OSV_LIBC_API
ssize_t tee(int fd_in, int fd_out, size_t len, unsigned int flags)
{
    // tee() duplicates pipe data from fd_in to fd_out without consuming it.
    // Preserving the input requires peeking, which OSv's pipe does not support,
    // so we cannot implement true (non-consuming) tee.  Report ENOSYS rather
    // than silently consuming the input (which would corrupt the caller's data
    // flow).
    (void)fd_in; (void)fd_out; (void)len; (void)flags;
    return libc_error(ENOSYS);
}
