/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Exercises close_range(2).  Built and run as part of the OSv test image.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <unistd.h>
#include <fcntl.h>

#include <errno.h>

#include <cassert>
#include <iostream>

#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC (1U << 2)
#endif

// Open n fds pointing at /dev/null and return the lowest one; they are
// contiguous because the fd table hands out the lowest free slots.
static int open_run(int n, int *fds)
{
    for (int i = 0; i < n; i++) {
        fds[i] = open("/dev/null", O_RDONLY);
        assert(fds[i] >= 0);
    }
    return fds[0];
}

int main()
{
    std::cerr << "Running close_range tests\n";

    // Close a contiguous run of fds.
    int fds[5];
    open_run(5, fds);
    assert(close_range(fds[0], fds[4], 0) == 0);
    // All of them are now closed: fcntl must report EBADF.
    for (int i = 0; i < 5; i++) {
        errno = 0;
        assert(fcntl(fds[i], F_GETFD) == -1 && errno == EBADF);
    }

    // A range that includes not-open fds is fine (Linux ignores them).
    int a = open("/dev/null", O_RDONLY);
    assert(a >= 0);
    assert(close_range(a, a + 100, 0) == 0);
    errno = 0;
    assert(fcntl(a, F_GETFD) == -1 && errno == EBADF);

    // CLOSE_RANGE_CLOEXEC sets close-on-exec instead of closing.
    int b = open("/dev/null", O_RDONLY);
    assert(b >= 0);
    assert((fcntl(b, F_GETFD) & FD_CLOEXEC) == 0);   // not set yet
    assert(close_range(b, b, CLOSE_RANGE_CLOEXEC) == 0);
    assert(fcntl(b, F_GETFD) & FD_CLOEXEC);          // now set
    assert(fcntl(b, F_GETFD) != -1);                 // still open
    close(b);

    // first > last is rejected; an unknown flag is rejected.
    errno = 0;
    assert(close_range(10, 5, 0) == -1 && errno == EINVAL);
    errno = 0;
    assert(close_range(0, 3, 0x40) == -1 && errno == EINVAL);

    std::cerr << "close_range tests PASSED\n";
    return 0;
}
