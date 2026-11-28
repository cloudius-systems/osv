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
//
// The pin rounds have the handler sleep first, and pin the child to another
// CPU with sched_setaffinity() while it sleeps.  The child may run during the
// sleep, but the count must not move during the spin after it: moving the
// child away from its handler would let both run at once.
//
// The masked rounds have a child send a signal to itself while it blocks it.
// The handler must not run until the child unblocks it, and must have run when
// sigprocmask() returns.
//
// The exit round has the handler end its own thread with SYS_exit.  The child
// must still be reaped.

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
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const int rounds = 100;
static const int pin_rounds = 20;
static const int masked_rounds = 20;

struct shared {
    std::atomic<long> count;        // the child's mainline progress
    std::atomic<long> moved;        // count changes seen during a handler
    std::atomic<int> handled;       // handlers that finished this round
    std::atomic<int> ready;
    std::atomic<int> stop;
    std::atomic<int> pin_round;     // the handler sleeps before it spins
    std::atomic<int> sleeping;      // the handler is in its sleep
    std::atomic<int> handler_cpu;
};
static shared *sh;

static long now_us()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
}

static void spin_us(long us)
{
    long end = now_us() + us;
    while (now_us() < end) {
    }
}

static void handler(int)
{
    if (sh->pin_round.load()) {
        sh->handler_cpu = sched_getcpu();
        sh->sleeping = 1;
        usleep(20000);
        sh->sleeping = 2;
    }
    long before = sh->count.load();
    spin_us(20000);
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

// Pin child @c to a CPU other than its handler's while the handler sleeps.
static void pin_during_sleep(pid_t c)
{
    long t0 = now_us();
    while (sh->sleeping.load() != 1 && now_us() - t0 < 5000000) {
        sched_yield();
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET((sh->handler_cpu.load() + 1) % sysconf(_SC_NPROCESSORS_ONLN), &set);
    if (sched_setaffinity(c, sizeof(set), &set) != 0) {
        printf("sched_setaffinity: errno %d\n", errno);
    }
}

// Run one round: returns 0 if the handler ran and the child did not.
static int one_round(int round, bool pin)
{
    sh->count = 0;
    sh->moved = 0;
    sh->handled = 0;
    sh->ready = 0;
    sh->stop = 0;
    sh->pin_round = pin;
    sh->sleeping = 0;
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
    if (pin) {
        pin_during_sleep(c);
    }
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

static std::atomic<int> *self_handled;

static void self_handler(int)
{
    (*self_handled)++;
}

// A child blocks @sig, sends it to itself, and unblocks it.  Returns 0 if the
// handler ran only once the signal was unblocked, before sigprocmask() returned.
static int masked_round(int round, int sig)
{
    *self_handled = 0;
    pid_t c = fork();
    if (c == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = self_handler;
        sigaction(sig, &sa, nullptr);
        sigset_t s;
        sigemptyset(&s);
        sigaddset(&s, sig);
        sigprocmask(SIG_BLOCK, &s, nullptr);
        kill(getpid(), sig);
        spin_us(20000);
        int masked = self_handled->load();
        sigprocmask(SIG_UNBLOCK, &s, nullptr);
        int unmasked = self_handled->load();
        _exit(masked == 0 && unmasked == 1 ? 0 : 10 + masked * 4 + unmasked);
    }
    if (c < 0) {
        printf("FAIL: fork: errno %d\n", errno);
        return 1;
    }
    int st;
    waitpid(c, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        int code = WIFEXITED(st) ? WEXITSTATUS(st) - 10 : -1;
        printf("round %d: signal %d: handled %d times while blocked, %d after "
               "sigprocmask(SIG_UNBLOCK)\n", round, sig, code / 4, code % 4);
        return 1;
    }
    return 0;
}

static void exiting_handler(int)
{
    sh->handled++;
    syscall(SYS_exit, 0);
}

// Returns 0 if a child whose handler ended its own thread is reaped.
static int exit_round()
{
    sh->handled = 0;
    sh->stop = 0;
    pid_t c = fork();
    if (c == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = exiting_handler;
        sigaction(SIGUSR1, &sa, nullptr);
        while (!sh->stop.load()) {
            usleep(1000);
        }
        _exit(0);
    }
    if (c < 0) {
        printf("FAIL: fork: errno %d\n", errno);
        return 1;
    }
    usleep(10000);
    kill(c, SIGUSR1);
    long t0 = now_us();
    while (!sh->handled.load() && now_us() - t0 < 5000000) {
        usleep(1000);
    }
    sh->stop = 1;
    int st;
    pid_t w = 0;
    t0 = now_us();
    while ((w = waitpid(c, &st, WNOHANG)) == 0 && now_us() - t0 < 10000000) {
        usleep(10000);
    }
    if (!sh->handled.load() || w != c) {
        printf("exit round: handled %d, waitpid %d: the child was not reaped\n",
               sh->handled.load(), w);
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

    void *m = mmap(nullptr, sizeof(shared) + sizeof(std::atomic<int>), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        printf("FAIL: mmap\n");
        return 1;
    }
    sh = new (m) shared{};
    self_handled = new (static_cast<shared *>(m) + 1) std::atomic<int>{0};
    int bad = 0;
    for (int r = 0; r < rounds; r++) {
        bad += one_round(r, false);
    }
    printf("%d rounds, %d with the child running during its handler\n", rounds, bad);
    int pin_bad = 0;
    if (sysconf(_SC_NPROCESSORS_ONLN) > 1) {
        for (int r = 0; r < pin_rounds; r++) {
            pin_bad += one_round(r, true);
        }
        printf("%d pin rounds, %d with the child moved or running during its "
               "handler\n", pin_rounds, pin_bad);
    } else {
        printf("pin rounds skipped: one CPU\n");
    }
    int masked_bad = 0;
    for (int r = 0; r < masked_rounds; r++) {
        masked_bad += masked_round(r, SIGUSR1);
        masked_bad += masked_round(r, SIGINT);
    }
    printf("%d masked rounds, %d with the handler run while blocked or not run "
           "when unblocked\n", 2 * masked_rounds, masked_bad);
    int exit_bad = exit_round();
    printf("exit round: %s\n", exit_bad ? "bad" : "ok");
    bad += pin_bad + masked_bad + exit_bad;
    if (bad) {
        printf("FAIL: tst-fork-signal-interrupts\n");
    } else {
        printf("PASS: tst-fork-signal-interrupts\n");
    }
    printf("SUMMARY: %d failures\n", bad ? 1 : 0);
    return bad ? 1 : 0;
}

#endif
