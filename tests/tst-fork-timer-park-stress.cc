/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * REGRESSION TEST for the fork timer-park intrusive-list double-insert (fork
 * WALL 2, see /tmp/pg-concurrent-fix.txt).
 *
 * The fork timer-park machinery (commit 1041518e5, core/sched.cc) removes an
 * app thread's app-stack sched::timer nodes from the per-CPU timer list on
 * switch-out and re-arms them on switch-in, and links the thread onto a
 * per-CPU cpu::parked_threads intrusive list.  Under SUSTAINED CONCURRENT load
 * (PostgreSQL: many backends in epoll_wait with timeouts, being load-balanced
 * across CPUs) a PARKED thread gets MIGRATED to another CPU while still linked
 * on its source CPU's parked list.  The single per-thread _timers_parked flag
 * and the per-CPU list then disagree: the thread is unparked/erased from the
 * WRONG CPU's list, or re-parked (push_back) while still linked, tripping
 *
 *   Assertion failed: !safemode_or_autounlink || node_algorithms::inited(...)
 *     boost/intrusive/list.hpp push_back:273
 *     sched::cpu::park_timers(sched::thread&)
 *     sched::cpu::reschedule_from_interrupt(...)
 *     sched::thread::wait() <- epoll_file::wait <- epoll_wait
 *
 * which aborts the whole unikernel.
 *
 * This test drives exactly that: after fork() (so a second address space
 * exists and park_timers actually does work), it spawns many worker threads
 * that each epoll_wait() with a short timeout in a tight loop, plus threads
 * that nanosleep() with jittered timeouts, so on -smp 2/4 the scheduler
 * continuously parks/unparks timers AND the load balancer migrates blocked
 * threads across CPUs.  On the buggy kernel the intrusive-list assert fires and
 * the VM aborts (the run harness sees a crash / missing final line).  With the
 * fix, every worker completes its rounds and the process exits cleanly.
 *
 * Best run under -smp 2 and -smp 4 (real concurrency + migration).
 */

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <atomic>

static const int N_EPOLL_THREADS = 16;
static const int N_SLEEP_THREADS = 8;
static const int ROUNDS = 300;

static std::atomic<int> failures{0};

static void short_sleep_ns(long ns)
{
    struct timespec ts { 0, ns };
    nanosleep(&ts, nullptr);
}

// Each epoll thread makes its own eventfd + epoll fd, then epoll_wait()s with a
// short timeout in a loop.  epoll_wait with a timeout arms an app-stack
// sched::timer and blocks -> the thread is parked; under load it is migrated
// while parked -> the double-insert hazard.  We occasionally poke the eventfd
// so some waits return via the fd instead of the timeout, mixing the paths.
static void *epoll_worker(void *arg)
{
    long id = (long)arg;
    int efd = eventfd(0, EFD_NONBLOCK);
    int ep = epoll_create1(0);
    if (efd < 0 || ep < 0) { failures++; return nullptr; }
    struct epoll_event ev { EPOLLIN, { .u64 = 0 } };
    epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev);
    struct epoll_event out[4];
    for (int i = 0; i < ROUNDS; i++) {
        // Timeout in the 1-7 ms range, jittered per thread so wakeups spread
        // across time and CPUs (maximizing park/unpark churn + migration).
        int timeout_ms = 1 + ((i + id) % 7);
        int n = epoll_wait(ep, out, 4, timeout_ms);
        if (n > 0) {
            uint64_t v;
            (void)!read(efd, &v, sizeof(v));
        }
        if ((i % 37) == 0) {
            uint64_t one = 1;
            (void)!write(efd, &one, sizeof(one));
        }
    }
    close(ep);
    close(efd);
    return nullptr;
}

// Sleep threads add pure nanosleep timer churn + block/wake so the load
// balancer has queued/waiting threads to migrate across CPUs.
static void *sleep_worker(void *arg)
{
    long id = (long)arg;
    for (int i = 0; i < ROUNDS * 4; i++) {
        short_sleep_ns((500 + ((i + id) % 11) * 250) * 1000L);  // 0.5-3.0 ms
    }
    return nullptr;
}

static int run_stress()
{
    pthread_t et[N_EPOLL_THREADS], st[N_SLEEP_THREADS];
    for (long i = 0; i < N_EPOLL_THREADS; i++)
        pthread_create(&et[i], nullptr, epoll_worker, (void*)i);
    for (long i = 0; i < N_SLEEP_THREADS; i++)
        pthread_create(&st[i], nullptr, sleep_worker, (void*)i);
    for (int i = 0; i < N_EPOLL_THREADS; i++) pthread_join(et[i], nullptr);
    for (int i = 0; i < N_SLEEP_THREADS; i++) pthread_join(st[i], nullptr);
    return failures.load();
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== tst-fork-timer-park-stress ===\n");

    // Fork first so a SECOND address space exists: park_timers only does real
    // work once there is more than AS0 (otherwise it is a cheap no-op and the
    // per-CPU list is always walked in the one-and-only AS).  The child runs
    // the same epoll/sleep stress in its own AS while the parent runs it in
    // AS0 -- the CPU cycles between address spaces AND migrates parked threads.
    pid_t pid = fork();
    if (pid == 0) {
        int f = run_stress();
        _exit(f == 0 ? 0 : 1);
    }
    if (pid < 0) { printf("FAIL: fork errno=%d\n", pid); return 1; }

    int f = run_stress();

    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    // Reaching here means no intrusive-list double-insert assert aborted the VM.
    if (w != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        printf("FAIL: child stress failed (exit=%d, wifexited=%d)\n",
               WIFEXITED(status) ? WEXITSTATUS(status) : -1, WIFEXITED(status));
        f++;
    }
    if (f == 0) {
        printf("PASS: sustained concurrent epoll/sleep timer churn across CPUs "
               "with fork -- no park_timers double-insert\n");
    }
    printf("=== tst-fork-timer-park-stress done: %d failures ===\n", f);
    return f == 0 ? 0 : 1;
}
