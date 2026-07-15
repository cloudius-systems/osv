/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Exercises epoll_pwait2(2) via the raw syscall.  Built and run on the OSv
// test image.

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>

#include <cassert>
#include <iostream>

static int epoll_pwait2_(int epfd, struct epoll_event *ev, int maxev,
                         const struct timespec *to, const sigset_t *sig)
{
    return syscall(SYS_epoll_pwait2, epfd, ev, maxev, to, sig);
}

int main()
{
    std::cerr << "Running epoll_pwait2 tests\n";

    int ep = epoll_create1(0);
    assert(ep >= 0);
    int efd = eventfd(0, EFD_NONBLOCK);
    assert(efd >= 0);

    struct epoll_event ev {};
    ev.events = EPOLLIN;
    ev.data.fd = efd;
    assert(epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev) == 0);

    struct epoll_event out[4];

    // Nothing ready: a zero timespec returns immediately with 0 events.
    struct timespec zero { 0, 0 };
    assert(epoll_pwait2_(ep, out, 4, &zero, nullptr) == 0);

    // Nothing ready: a short timeout returns 0 after roughly that long.
    struct timespec ts { 0, 100 * 1000 * 1000 };  // 100 ms
    struct timespec before, after;
    clock_gettime(CLOCK_MONOTONIC, &before);
    assert(epoll_pwait2_(ep, out, 4, &ts, nullptr) == 0);
    clock_gettime(CLOCK_MONOTONIC, &after);
    long long elapsed_ms = (after.tv_sec - before.tv_sec) * 1000 +
                           (after.tv_nsec - before.tv_nsec) / 1000000;
    assert(elapsed_ms >= 90);   // waited about 100 ms, not returned instantly

    // Make the eventfd readable: pwait2 reports it.
    uint64_t one = 1;
    assert(write(efd, &one, sizeof(one)) == sizeof(one));
    int n = epoll_pwait2_(ep, out, 4, &ts, nullptr);
    assert(n == 1);
    assert(out[0].data.fd == efd);
    assert(out[0].events & EPOLLIN);

    // NULL timeout with a ready fd returns immediately (does not block forever).
    n = epoll_pwait2_(ep, out, 4, nullptr, nullptr);
    assert(n == 1);

    close(efd);
    close(ep);
    std::cerr << "epoll_pwait2 tests PASSED\n";
    return 0;
}
