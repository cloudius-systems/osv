/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Exercises the relaxed tgkill(2) and the rt_sigtimedwait(2) timeout path.
// Built and run on the OSv test image.

#include <sys/syscall.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#include <cassert>
#include <atomic>
#include <iostream>

static std::atomic<int> g_caught{0};
static void handler(int) { g_caught.fetch_add(1, std::memory_order_relaxed); }

static long gettid_() { return syscall(SYS_gettid); }

int main()
{
    std::cerr << "Running signal-fills tests\n";

    // ---- tgkill ----
    struct sigaction sa {};
    sa.sa_handler = handler;
    sigaction(SIGUSR1, &sa, nullptr);

    // tgkill to our own tgid/tid delivers the signal.
    g_caught = 0;
    assert(syscall(SYS_tgkill, getpid(), gettid_(), SIGUSR1) == 0);
    for (int i = 0; i < 100 && g_caught == 0; i++) usleep(1000);
    assert(g_caught >= 1);

    // tgkill with a wrong tgid -> ESRCH.
    errno = 0;
    assert(syscall(SYS_tgkill, getpid() + 12345, gettid_(), SIGUSR1) == -1 && errno == ESRCH);

    // tgkill to a non-existent tid -> ESRCH (not a crash / not silently OK).
    errno = 0;
    assert(syscall(SYS_tgkill, getpid(), 0x7fffff, SIGUSR1) == -1 && errno == ESRCH);

    // tgkill targeting another *live* thread is accepted and the signal is
    // delivered process-wide (OSv's delivery model).
    pthread_t th;
    struct thread_args {
        std::atomic<long> tid{0};
        std::atomic<bool> stop{false};
    } targs;
    // targs lives on this stack frame until pthread_join() below, so the child
    // can safely reference it; no heap allocation to leak.
    int rc = pthread_create(&th, nullptr, [](void *p) -> void * {
        auto *a = static_cast<thread_args *>(p);
        a->tid.store(syscall(SYS_gettid));
        while (!a->stop.load()) usleep(1000);
        return nullptr;
    }, &targs);
    assert(rc == 0);
    while (targs.tid == 0) usleep(1000);
    g_caught = 0;
    assert(syscall(SYS_tgkill, getpid(), targs.tid.load(), SIGUSR1) == 0);
    for (int i = 0; i < 100 && g_caught == 0; i++) usleep(1000);
    assert(g_caught >= 1);
    targs.stop = true;
    pthread_join(th, nullptr);

    // ---- rt_sigtimedwait timeout path ----
    // Block SIGUSR2, then rt_sigtimedwait with a short timeout and no pending
    // signal -> times out with EAGAIN (previously returned ENOSYS).
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR2);
    sigprocmask(SIG_BLOCK, &set, nullptr);

    struct timespec ts { 0, 150 * 1000 * 1000 };  // 150 ms
    siginfo_t si;
    struct timespec before, after;
    clock_gettime(CLOCK_MONOTONIC, &before);
    errno = 0;
    int r = syscall(SYS_rt_sigtimedwait, &set, &si, &ts, sizeof(sigset_t));
    clock_gettime(CLOCK_MONOTONIC, &after);
    assert(r == -1 && errno == EAGAIN);
    long long ms = (after.tv_sec - before.tv_sec) * 1000 +
                   (after.tv_nsec - before.tv_nsec) / 1000000;
    assert(ms >= 130);   // actually waited ~150 ms, did not return ENOSYS instantly

    std::cerr << "signal-fills tests PASSED\n";
    return 0;
}
