/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Interval timers belong to a process.  Under fork() each child must get its
// own ITIMER_REAL: one child's setitimer() must not cancel another's, the
// SIGALRM must run that child's handler, a child must not inherit its parent's
// timer, and a parent that set no timer must get no SIGALRM.  PostgreSQL drives
// statement_timeout, lock_timeout and deadlock_timeout through this timer, in
// backends whose postmaster ignores SIGALRM.  Its statement and lock timeout
// handlers then send SIGINT to their own pid, which must reach the backend and
// not the postmaster (whose SIGINT handler starts a fast shutdown).

#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <cstdio>
#include <cstdlib>

#include <osv/kernel_config_fork.h>

#if !CONF_fork
int main()
{
    printf("tst-fork-itimer: CONF_fork disabled, not applicable; SKIP\n");
    return 0;
}
#else

static volatile int g_count;
static volatile long g_first_ms;

static long now_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void on_alarm(int)
{
    if (g_count++ == 0) {
        g_first_ms = now_ms();
    }
}

static volatile int g_sigint;

static void on_sigint(int)
{
    g_sigint++;
}

static void catch_alarm()
{
    struct sigaction sa = {};
    sa.sa_handler = on_alarm;
    sigaction(SIGALRM, &sa, nullptr);
}

// Sleep until @until_ms, so that a late or unwanted signal is seen.
static void settle(long until_ms)
{
    while (now_ms() < until_ms) {
        usleep(10000);
    }
}

// Child body: arm ITIMER_REAL (or alarm()) for @ms, wait, and exit 0 only
// if exactly one SIGALRM ran our handler at @ms +-50.
static int child_timer(long ms, bool use_alarm)
{
    g_count = 0;
    g_first_ms = 0;
    catch_alarm();
    struct itimerval cur;
    if (getitimer(ITIMER_REAL, &cur) != 0 ||
        cur.it_value.tv_sec || cur.it_value.tv_usec) {
        printf("  child %d: inherited a running timer (%ld.%06ld s left)\n",
               getpid(), (long)cur.it_value.tv_sec, (long)cur.it_value.tv_usec);
        return 3;
    }
    long t0 = now_ms();
    if (use_alarm) {
        alarm(ms / 1000);
    } else {
        struct itimerval it = {};
        it.it_value.tv_sec = ms / 1000;
        it.it_value.tv_usec = (ms % 1000) * 1000;
        if (setitimer(ITIMER_REAL, &it, nullptr) != 0) {
            return 4;
        }
    }
    // OSv runs the handler on its own thread, so poll for it rather than
    // pause(): a child whose alarm never comes must exit and fail, not stay
    // in pause() and keep the guest from shutting down.
    long deadline = t0 + ms + 1000;
    while (g_count == 0 && now_ms() < deadline) {
        usleep(2000);
    }
    // Give a second, unwanted SIGALRM time to show.
    settle(now_ms() + 300);
    long at = g_first_ms ? g_first_ms - t0 : -1;
    printf("  child %d: %d SIGALRM, first at %ld ms (want 1 at %ld +-50)\n",
           getpid(), g_count, at, ms);
    return (g_count == 1 && at >= ms - 50 && at <= ms + 50) ? 0 : 1;
}

static pid_t spawn(long ms, bool use_alarm)
{
    pid_t p = fork();
    if (p == 0) {
        _exit(child_timer(ms, use_alarm));
    }
    return p;
}

// Wait at most 3 s for @p; -2 if it is still running (its alarm never came).
static int reap(pid_t p)
{
    long deadline = now_ms() + 3000;
    int st = 0;
    while (true) {
        pid_t r = waitpid(p, &st, WNOHANG);
        if (r == p) {
            return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        }
        if (r != 0 || now_ms() > deadline) {
            printf("  child %d: no exit within 3 s\n", p);
            return -2;
        }
        usleep(10000);
    }
}

static int failures;

static void check(bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

int main()
{
    // 1. Two children, 200 ms and 400 ms; the parent catches SIGALRM but sets
    //    no timer and must see none.
    catch_alarm();
    g_count = 0;
    pid_t a = spawn(200, false);
    pid_t b = spawn(400, false);
    int ra = reap(a), rb = reap(b);
    settle(now_ms() + 200);
    check(ra == 0, "child A got its own 200 ms SIGALRM");
    check(rb == 0, "child B got its own 400 ms SIGALRM");
    check(g_count == 0, "the parent, which set no timer, got no SIGALRM");
    printf("  parent got %d SIGALRM\n", g_count);

    // 2. alarm() in a child whose parent ignores SIGALRM (the postmaster).
    signal(SIGALRM, SIG_IGN);
    pid_t c = spawn(1000, true);
    check(reap(c) == 0, "alarm(1) in a child of a parent that ignores SIGALRM");

    // 3. A child does not inherit its parent's pending timer, and the parent
    //    still gets its own.
    catch_alarm();
    g_count = 0;
    g_first_ms = 0;
    struct itimerval it = {};
    it.it_value.tv_usec = 300000;
    long t0 = now_ms();
    setitimer(ITIMER_REAL, &it, nullptr);
    pid_t d = fork();
    if (d == 0) {
        g_count = 0;
        struct itimerval cur;
        getitimer(ITIMER_REAL, &cur);
        bool clear = !cur.it_value.tv_sec && !cur.it_value.tv_usec;
        settle(now_ms() + 600);
        _exit(clear && g_count == 0 ? 0 : 1);
    }
    int rd = reap(d);
    settle(t0 + 700);
    long at = g_first_ms ? g_first_ms - t0 : -1;
    check(rd == 0, "a child does not inherit or receive its parent's timer");
    check(g_count == 1 && at >= 250 && at <= 350,
          "the parent got its own 300 ms SIGALRM");
    printf("  parent got %d SIGALRM, first at %ld ms\n", g_count, at);

    // 4. A child's signal to its own pid runs the child's handler, not the
    //    parent's.
    struct sigaction si = {};
    si.sa_handler = on_sigint;
    sigaction(SIGINT, &si, nullptr);
    g_sigint = 0;
    pid_t e = fork();
    if (e == 0) {
        g_sigint = 0;
        kill(getpid(), SIGINT);
        settle(now_ms() + 300);
        _exit(g_sigint == 1 ? 0 : 1);
    }
    int re = reap(e);
    settle(now_ms() + 200);
    check(re == 0, "a child's kill(getpid(), SIGINT) ran the child's handler");
    check(g_sigint == 0, "the parent's SIGINT handler did not run");
    printf("  parent got %d SIGINT\n", g_sigint);

    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
#endif
