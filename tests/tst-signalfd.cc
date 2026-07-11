/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Exercises signalfd(2): a signal raised while a signalfd watches it is
// delivered to the fd (and consumed) rather than running the default action.
// Built and run as part of the OSv test image.

#include <sys/signalfd.h>
#include <sys/poll.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include <cassert>
#include <cstring>
#include <iostream>

int main()
{
    std::cerr << "Running signalfd tests\n";

    // Create a signalfd watching SIGUSR1 and SIGUSR2.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    int sfd = signalfd(-1, &mask, SFD_NONBLOCK);
    assert(sfd >= 0);

    // Nothing pending yet: a nonblocking read returns EAGAIN.
    signalfd_siginfo si;
    ssize_t n = read(sfd, &si, sizeof(si));
    assert(n == -1 && errno == EAGAIN);

    // Raise SIGUSR1: it must be delivered to the fd, not the default action
    // (which would power the guest off).
    assert(kill(getpid(), SIGUSR1) == 0);

    n = read(sfd, &si, sizeof(si));
    assert(n == (ssize_t)sizeof(si));
    assert(si.ssi_signo == (uint32_t)SIGUSR1);

    // Raise SIGUSR2, then verify poll() reports it readable, then read it.
    assert(kill(getpid(), SIGUSR2) == 0);
    struct pollfd pfd { sfd, POLLIN, 0 };
    assert(poll(&pfd, 1, 1000) == 1);
    assert(pfd.revents & POLLIN);
    n = read(sfd, &si, sizeof(si));
    assert(n == (ssize_t)sizeof(si));
    assert(si.ssi_signo == (uint32_t)SIGUSR2);

    // Two signals queued before a read: both should come out in order.
    assert(kill(getpid(), SIGUSR1) == 0);
    assert(kill(getpid(), SIGUSR2) == 0);
    signalfd_siginfo two[2];
    n = read(sfd, two, sizeof(two));
    assert(n == (ssize_t)sizeof(two));
    assert(two[0].ssi_signo == (uint32_t)SIGUSR1);
    assert(two[1].ssi_signo == (uint32_t)SIGUSR2);

    // A short buffer (< one siginfo) is rejected with EINVAL.
    char tiny[8];
    n = read(sfd, tiny, sizeof(tiny));
    assert(n == -1 && errno == EINVAL);

    // Bad flags rejected.
    errno = 0;
    assert(signalfd(-1, &mask, 0x40) == -1 && errno == EINVAL);

    close(sfd);
    std::cerr << "signalfd tests PASSED\n";
    return 0;
}
