/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Regression test for the fork/exit fd-table teardown leak (T-FDLEAK).
 *
 * OSv keeps ONE global fd table (fs/vfs/kern_descrip.cc gfdt[]) shared by a
 * fork parent and every child.  fork() (libc/process/fork.cc) fakes POSIX fd
 * inheritance with per-address-space bookkeeping, and the reaper closure runs
 * release_inherited_fds(child_as) when a forked child is reaped.
 *
 * Before the fix, release_inherited_fds() dropped only the INHERITED fd
 * references and never closed the fds the child opened for ITSELF after fork.
 * On real Linux process exit closes the whole fd table; OSv's shared-table
 * reaper leaked every child-opened fd into gfdt.  Across sustained fork/reap
 * churn gfdt filled toward FDMAX and fdalloc() returned EMFILE -- exactly the
 * wedge PostgreSQL hit (pipe()/epoll_create1()/accept() all EMFILE) after a
 * few thousand fork-per-connection backends, each of which opens its own
 * latch self-pipe + WaitEventSet epoll fd and exits.
 *
 * This test reproduces the leak WITHOUT PostgreSQL: a tight loop forks, the
 * child opens a pipe (2 fds) + an epoll fd (1 fd) -- mirroring PG's per-backend
 * self-pipe + WaitEventSet -- and _exit()s WITHOUT closing them; the parent
 * waitpid()s and repeats.  It samples the guest's open-fd count (a probe of the
 * shared gfdt via fcntl(F_GETFD)) and asserts it returns to a flat baseline
 * once the children are reaped.
 *
 *   before the fix: the count climbs ~linearly (each reaped child leaks 3 fds)
 *                   until it approaches FDMAX;
 *   after the fix:  the count is FLAT at baseline across tens of thousands of
 *                   cycles.
 *
 * Run (CONF_fork=y build):
 *   ./scripts/build conf_fork=1 image=tests
 *   ./scripts/run.py -c 2 -e "/tests/tst-fork-fdleak.so <cycles>"
 *
 * Requires CONF_fork (fork() is a stub otherwise); without it the test exits 0
 * after printing that it is not applicable.
 */

#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <osv/kernel_config_fork.h>

// Count the open fds visible in the (single, global) OSv fd table by probing
// fcntl(fd, F_GETFD) across [0, cap).  gfdt is process-global on OSv, so this
// reflects the whole guest's fd occupancy -- which is precisely what leaks.
// cap is kept well above the baseline (a few hundred) but far below FDMAX so a
// sample is cheap; the leak, if present, climbs past it quickly and the probe
// still shows the unmistakable upward trend.
static int count_open_fds(int cap)
{
    int n = 0;
    for (int fd = 0; fd < cap; fd++) {
        if (fcntl(fd, F_GETFD) != -1 || errno != EBADF) {
            n++;
        }
    }
    return n;
}

#if !CONF_fork
int main()
{
    printf("tst-fork-fdleak: CONF_fork disabled, not applicable; SKIP\n");
    return 0;
}
#else

// One churn cycle: fork; the child opens fds it never closes, then _exit()s;
// the parent reaps it.  Returns 0 on success, nonzero on a harness failure
// (fork/waitpid error) -- NOT on a leak (the leak shows up in the fd count).
static int churn_once()
{
    pid_t pid = fork();
    if (pid < 0) {
        // EMFILE here means the PARENT's fork path could not get resources --
        // itself a symptom of exhaustion; report it so the caller can stop.
        return -1;
    }
    if (pid == 0) {
        // Child: open the fds a forked PG backend opens for itself after fork
        // and would normally close at exit -- a self-pipe (pipe => 2 fds) and a
        // WaitEventSet epoll fd (1 fd) -- then _exit WITHOUT closing them.  On
        // real Linux the kernel closes them at exit; on OSv the reaper must.
        int p[2];
        if (pipe(p) == 0) {
            // leave p[0], p[1] open on purpose
        }
        int ep = epoll_create1(0);
        (void)ep;   // leave it open on purpose
        _exit(0);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) != pid) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    long cycles = 5000;
    if (argc > 1) {
        cycles = strtol(argv[1], nullptr, 10);
        if (cycles <= 0) cycles = 5000;
    }
    // Probe window: generous headroom over the baseline, cheap to scan.
    const int PROBE_CAP = 8192;

    printf("=== tst-fork-fdleak: %ld fork+open+exit cycles ===\n", cycles);
    fflush(stdout);

    // Warm up one cycle so any one-time allocations settle, then take baseline.
    if (churn_once() != 0) {
        printf("FAIL: warm-up churn failed (errno=%d)\n", errno);
        return 1;
    }
    int baseline = count_open_fds(PROBE_CAP);
    printf("baseline open fds after warm-up: %d\n", baseline);
    fflush(stdout);

    // The leak is 3 fds/cycle; a "leaking" verdict is a count that grew far past
    // what transient in-flight fds could explain.  Allow generous slack for any
    // legitimately-open transient fds, but a real leak blows through it.
    const int SLACK = 64;
    int worst = baseline;
    int sample_every = cycles / 20;
    if (sample_every < 1) sample_every = 1;

    for (long i = 1; i <= cycles; i++) {
        int rc = churn_once();
        if (rc != 0) {
            int now = count_open_fds(PROBE_CAP);
            printf("FAIL: churn cycle %ld failed (errno=%d, EMFILE=%d); "
                   "open fds now=%d baseline=%d -- fd EXHAUSTION / LEAK\n",
                   i, errno, errno == EMFILE, now, baseline);
            return 1;
        }
        if (i % sample_every == 0 || i == cycles) {
            int now = count_open_fds(PROBE_CAP);
            if (now > worst) worst = now;
            printf("cycle %6ld: open fds = %d (baseline %d, delta %+d)\n",
                   i, now, baseline, now - baseline);
            fflush(stdout);
        }
    }

    int final_fds = count_open_fds(PROBE_CAP);
    int delta = final_fds - baseline;
    printf("final open fds = %d, baseline = %d, delta = %+d, worst = %d\n",
           final_fds, baseline, delta, worst);

    bool leaked = (delta > SLACK) || (worst - baseline > SLACK);
    if (leaked) {
        printf("FAIL: fd count did NOT return to baseline after reaping "
               "%ld children -- LEAK (delta %+d, slack %d)\n",
               cycles, delta, SLACK);
        printf("=== tst-fork-fdleak: FAILED ===\n");
        return 1;
    }

    printf("PASS: fd count flat at baseline across %ld fork+open+exit cycles\n",
           cycles);
    printf("=== tst-fork-fdleak: OK ===\n");
    return 0;
}
#endif // CONF_fork
