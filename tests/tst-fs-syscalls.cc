/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Exercises preadv2/pwritev2 and renameat2 RENAME_NOREPLACE.
// Built and run on the OSv test image.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include <cassert>
#include <cstring>
#include <iostream>

// Shared flag definitions and prototypes (kept in sync with fs/vfs/main.cc).
#include <osv/fs_flags.h>

int main()
{
    std::cerr << "Running fs-syscalls tests\n";

    const char *path = "/tmp/fssc-test";
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    assert(fd >= 0);

    // pwritev2(flags=0) then preadv2(flags=0) round-trip at an offset.
    const char *msg = "hello preadv2";
    struct iovec wio { (void *)msg, strlen(msg) };
    assert(pwritev2(fd, &wio, 1, 0, 0) == (ssize_t)strlen(msg));
    char buf[64] = {};
    struct iovec rio { buf, sizeof(buf) };
    ssize_t r = preadv2(fd, &rio, 1, 0, 0);
    assert(r == (ssize_t)strlen(msg));
    assert(memcmp(buf, msg, strlen(msg)) == 0);

    // RWF_APPEND (one-shot append without O_APPEND): OSv rejects it with
    // EOPNOTSUPP (it cannot be done atomically against concurrent writers via
    // the current fops path, so it is not offered racily; callers open with
    // O_APPEND for atomic append).  On Linux it is supported and atomic, so
    // accept either outcome to keep this test meaningful on both.
    const char *tail = "APPENDED";
    struct iovec aio { (void *)tail, strlen(tail) };
    errno = 0;
    ssize_t ar = pwritev2(fd, &aio, 1, 0, RWF_APPEND);
    assert((ar == -1 && errno == EOPNOTSUPP) || ar == (ssize_t)strlen(tail));

    // RWF_DSYNC just forces a flush; still succeeds.
    assert(pwritev2(fd, &wio, 1, 0, RWF_DSYNC) == (ssize_t)strlen(msg));

    // RWF_NOWAIT can't be honored on a blocking file path.  Rather than fake
    // EAGAIN (which would make a retrying caller spin forever), reject it.
    errno = 0;
    assert(preadv2(fd, &rio, 1, 0, RWF_NOWAIT) == -1 && errno == EOPNOTSUPP);

    // An unsupported flag -> EOPNOTSUPP.
    errno = 0;
    assert(preadv2(fd, &rio, 1, 0, 0x8000) == -1 && errno == EOPNOTSUPP);
    close(fd);

    // renameat2 RENAME_NOREPLACE.
    const char *a = "/tmp/fssc-a";
    const char *b = "/tmp/fssc-b";
    int fa = open(a, O_CREAT | O_TRUNC | O_WRONLY, 0644); assert(fa >= 0); close(fa);
    unlink(b);
    // dest does not exist -> NOREPLACE rename succeeds.
    assert(renameat2(AT_FDCWD, a, AT_FDCWD, b, RENAME_NOREPLACE) == 0);
    // recreate source; dest now exists -> NOREPLACE fails EEXIST.
    fa = open(a, O_CREAT | O_TRUNC | O_WRONLY, 0644); assert(fa >= 0); close(fa);
    errno = 0;
    assert(renameat2(AT_FDCWD, a, AT_FDCWD, b, RENAME_NOREPLACE) == -1 && errno == EEXIST);
    // plain renameat2 (flags 0) still replaces.
    assert(renameat2(AT_FDCWD, a, AT_FDCWD, b, 0) == 0);
    // RENAME_EXCHANGE is not supported -> EINVAL.
    errno = 0;
    assert(renameat2(AT_FDCWD, b, AT_FDCWD, b, RENAME_EXCHANGE) == -1 && errno == EINVAL);
    unlink(b);

    std::cerr << "fs-syscalls tests PASSED\n";
    return 0;
}
