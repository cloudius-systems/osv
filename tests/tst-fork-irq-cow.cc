/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Regression test for the fork "IRQ-context COW / foreign-address-space timer"
 * wall (see /tmp/pg-irqcow-fix.txt).
 *
 * OSv keeps ONE per-CPU timer list (identity kernel memory) walked from the
 * timer IRQ in WHATEVER address space the CPU currently holds.  An application
 * thread arms a sched::timer on its (per-address-space private) user stack --
 * e.g. every nanosleep()/poll()/epoll_wait() timeout does.  After fork() the
 * parent and each child have DIFFERENT physical stacks at the SAME virtual
 * address.  A timer armed by a fork child, left in the shared per-CPU list
 * while the CPU runs the parent (or the idle thread in AS0), is dereferenced
 * through a FOREIGN address space: its stack VA resolves to unrelated physical
 * memory, so the intrusive-list walk in timer_set::expire() follows a wild
 * next/prev pointer and takes a page fault in NON-PREEMPTABLE interrupt context
 * -> assert(sched::preemptable()) in arch/x64/mmu.cc:38, which aborts the whole
 * unikernel.  PostgreSQL hit this within tens of ms of "ready to accept
 * connections": a forked aux process armed a WaitLatch timeout, blocked, the
 * CPU idled, and the idle-CPU timer IRQ walked the child's timer node in AS0.
 *
 * The fix parks an app thread's timers off the per-CPU list on switch-out (in
 * its own AS, where its stack timers are valid) and re-arms them on switch-in;
 * a per-CPU identity kernel timer still wakes a blocked parked thread on time.
 *
 * This test drives exactly that hazard: a fork child sleeps repeatedly (arming
 * and expiring app-stack timers) while the parent also sleeps/works, so the CPU
 * continuously cycles parent-AS <-> child-AS <-> idle-AS0 and fires timer IRQs
 * in an address space that is NOT the one that armed the pending timer.  On the
 * buggy kernel the unikernel aborts on the preemptable() assert (no in-process
 * SIGSEGV to catch -- the whole VM dies), so a broken fix shows up as the test
 * BINARY never printing its final line / the run harness reporting a crash.
 * With the fix, both processes complete all their timed sleeps and the child's
 * exit code is delivered.
 *
 * Best reproduced on -smp 1 (forces the idle CPU, in AS0, to fire the child's
 * timer) and also run on -smp 2.
 */

#include <unistd.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <time.h>
#include <cstdio>
#include <cstdint>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

enum { CHILD_OK = 71 };

// How many short timed sleeps each side performs.  Each nanosleep arms a
// sched::timer on the caller's app stack and blocks -- so while the sleeper is
// off-CPU, its timer sits in the per-CPU list and the CPU runs another AS/idle.
static const int ROUNDS = 400;
static const long SLEEP_NS = 1 * 1000 * 1000;   // 1 ms

static void timed_sleeps(int rounds, long ns)
{
    struct timespec ts;
    for (int i = 0; i < rounds; ++i) {
        ts.tv_sec = 0;
        ts.tv_nsec = ns;
        // nanosleep arms a stack sched::timer and blocks -> the classic case.
        nanosleep(&ts, nullptr);
    }
}

int main()
{
    printf("=== tst-fork-irq-cow ===\n");

    pid_t pid = fork();
    if (pid == 0) {
        // CHILD (its own address space): hammer timed sleeps.  Every sleep
        // leaves a timer armed on the child's private stack while the child is
        // blocked and the CPU runs the parent or idles in AS0.  On the buggy
        // kernel a timer IRQ then walks this node through a foreign AS and the
        // unikernel aborts on the preemptable() assert.
        timed_sleeps(ROUNDS, SLEEP_NS);
        _exit(CHILD_OK);
    }
    CHECK(pid > 0, "fork() returned child pid to parent");
    if (pid <= 0) {
        printf("=== tst-fork-irq-cow done: %d failures ===\n", failures);
        return 1;
    }

    // PARENT (AS0): also sleep repeatedly so the CPU keeps switching between the
    // parent's AS, the child's AS, and the idle thread (AS0) -- maximizing the
    // number of timer IRQs that fire in an AS other than the one whose timer is
    // pending.  Real timed sleeps, no busy spin, so on -smp 1 the CPU genuinely
    // idles between wakeups and the idle-CPU timer IRQ path is exercised.
    timed_sleeps(ROUNDS, SLEEP_NS);

    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    CHECK(w == pid, "waitpid() reaped the timer-sleeping fork child");
    // Reaching here at all means no preemptable() assert fired: a foreign-AS
    // timer-IRQ COW fault would have aborted the unikernel before this point.
    CHECK(WIFEXITED(status),
          "child exited normally (no foreign-AS timer-IRQ COW abort)");
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == CHILD_OK,
          "child completed all its timed sleeps under cross-AS timer IRQs");

    printf("=== tst-fork-irq-cow done: %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
