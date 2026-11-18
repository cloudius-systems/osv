/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A signal sent to another process must interrupt that process's thread, not
// run beside it.
//
// POSIX delivers an asynchronous signal by interrupting a thread of the
// target process: while the handler runs, that thread does not.  Programs
// rely on this.  PostgreSQL's interrupt flags are one example:
//
//   handler:   InterruptPending = 1; MessagePending = 1; SetLatch();
//   mainline:  if (InterruptPending) { InterruptPending = 0;
//                  if (MessagePending) { MessagePending = 0; handle(); } }
//
// If the handler can run at the same time as the mainline, it can set both
// flags between the mainline's clear and its test, leaving MessagePending set
// with InterruptPending clear.  The message is never handled and the backend
// sleeps on its latch for good, as a parallel CREATE INDEX leader did,
// waiting in ParallelFinish for workers that had long exited.
//
// Each round forks a single-threaded child that counts in a loop.  The parent
// signals it; the handler reads the count, spins for 20 ms, and reads it
// again.  The count must not move while the handler runs.  A round fails if it
// did, or if the handler never ran.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-signal-interrupts requires CONF_fork\n");
    return 0;
}

#else

#include <atomic>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const int rounds = 100;

struct shared {
    std::atomic<long> count;        // the child's mainline progress
    std::atomic<long> moved;        // count changes seen during a handler
    std::atomic<int> handled;       // handlers that finished this round
    std::atomic<int> ready;
    std::atomic<int> stop;
};
static shared *sh;

static long now_us()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
}

static void handler(int)
{
    long before = sh->count.load();
    long end = now_us() + 20000;
    while (now_us() < end) {
    }
    long after = sh->count.load();
    if (after != before) {
        sh->moved += after - before;
    }
    sh->handled++;
}

static void child_main()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigaction(SIGUSR1, &sa, nullptr);
    sh->ready = 1;
    while (!sh->stop.load()) {
        sh->count++;
    }
    _exit(0);
}

// Run one round: returns 0 if the handler ran and the child did not.
static int one_round(int round)
{
    sh->count = 0;
    sh->moved = 0;
    sh->handled = 0;
    sh->ready = 0;
    sh->stop = 0;
    pid_t c = fork();
    if (c == 0) {
        child_main();
    }
    if (c < 0) {
        printf("FAIL: fork: errno %d\n", errno);
        return 1;
    }
    while (!sh->ready.load()) {
        sched_yield();
    }
    // Let the child run before the signal arrives.
    long start = sh->count.load();
    while (sh->count.load() < start + 1000) {
        sched_yield();
    }
    kill(c, SIGUSR1);
    long t0 = now_us();
    while (!sh->handled.load() && now_us() - t0 < 5000000) {
        usleep(1000);
    }
    sh->stop = 1;
    int st;
    waitpid(c, &st, 0);
    if (!sh->handled.load()) {
        printf("round %d: the handler never ran\n", round);
        return 1;
    }
    if (sh->moved.load()) {
        printf("round %d: the child counted %ld while its handler ran\n", round,
               sh->moved.load());
        return 1;
    }
    return 0;
}

int main()
{
    errno = 0;
    pid_t probe = fork();
    if (probe == -1 && errno == ENOSYS) {
        printf("SKIP: fork() not enabled\n");
        return 0;
    }
    if (probe == 0) {
        _exit(0);
    }
    waitpid(probe, nullptr, 0);

    void *m = mmap(nullptr, sizeof(shared), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        printf("FAIL: mmap\n");
        return 1;
    }
    sh = new (m) shared{};
    int bad = 0;
    for (int r = 0; r < rounds; r++) {
        bad += one_round(r);
    }
    printf("%d rounds, %d with the child running during its handler\n", rounds, bad);
    if (bad) {
        printf("FAIL: tst-fork-signal-interrupts\n");
    } else {
        printf("PASS: tst-fork-signal-interrupts\n");
    }
    printf("SUMMARY: %d failures\n", bad ? 1 : 0);
    return bad ? 1 : 0;
}

#endif
