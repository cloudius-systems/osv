/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Regression test for the preemption-dependent context-integrity bug in the
 * same-VA fork child context-switch path (see documentation/fork.md and the
 * deferred-CR3 switch in arch/x64/arch-switch.hh).
 *
 * The existing fork tests (tst-fork / tst-fork-cow / tst-fork-deep / tst-pgfork
 * / tst-execve) all fork a child that does a BOUNDED amount of work and exits
 * quickly, so the child is preempted at most a handful of times.  The real
 * PostgreSQL checkpointer child, by contrast, is LONG-LIVED and runs on the
 * parent's exact stack VAs while being preempted hundreds of times under
 * concurrent load from the parent + kernel threads.  That scenario -- "a forked
 * child that survives many preemptive context switches while running on the
 * parent's stack VAs" -- was untested, and that is exactly where the
 * context-integrity corruption (app rip/rsp reconstructed from .text -> wild
 * jump -> SIGSEGV) manifests.
 *
 * This test reproduces that pressure: it forks a child that spins for several
 * seconds in a compute+allocate loop that touches .data/.bss globals AND its
 * own stack, while the PARENT stays runnable (also spinning).  On a single CPU
 * this forces the scheduler to preempt the child in and out of its same-VA
 * stack hundreds of times.  The child continuously verifies its own state
 * (a stack-local running checksum kept in step with a .bss checksum) and exits
 * with a known code; the parent waitpid-verifies that code.  If the child's
 * context is ever corrupted by a preemptive switch, it crashes (SIGSEGV) or
 * exits with a mismatch code, and the parent flags the failure.
 */

#include <unistd.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cmath>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

// Globals in .data and .bss the child mutates while being preempted.  If a
// preemptive switch corrupts the child's context these will diverge from the
// stack-local shadow, or the child will crash before it can compare them.
static volatile uint64_t g_data_accum = 0x0123456789abcdefULL;  // .data
static volatile uint64_t g_bss_accum;                           // .bss (zero)

// Child exit codes (kept small; only the low 8 bits survive waitpid).
enum { CHILD_OK = 55, CHILD_MISMATCH = 66 };

// How long the child spins.  Long enough to be preempted many hundreds of times
// under the default OSv preemption tick while the parent competes for the CPU.
static const int SPIN_SECONDS = 6;

// Depth of the recursive stack the child spins at, so it is preempted while deep
// in nested frames on its same-VA stack (like the checkpointer, which spins deep
// inside InitAuxiliaryProcess / CheckpointerMain).
static const int RECURSE_DEPTH = 64;

static uint64_t mix(uint64_t x)
{
    // A cheap, order-sensitive scramble so the accumulator depends on every
    // iteration (splitmix64-style); nothing crypto, just enough that a dropped
    // or duplicated iteration from a corrupt resume changes the result.
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// The child's spin loop, run at the bottom of a deep recursion so preemption
// catches it deep in nested same-VA stack frames.  Kept in its own function so
// it has a real stack frame (locals live across the many preemptions), and so a
// wild rip is more likely to land somewhere fatal rather than silently continue.
static int child_spin_leaf()
{
    // Stack-local shadow of the .bss accumulator.  Both are updated in lockstep
    // every iteration; a corrupt resume that drops us mid-instruction or with a
    // wild stack pointer will desynchronize them (or crash outright).
    volatile uint64_t stack_shadow = 0;
    volatile uint64_t bss_local = 0;
    // Exercise FPU/SSE too: the preemptive switch path saves/restores the FPU
    // control word and MXCSR, and a same-VA-stack corruption there is the prime
    // suspect, so keep live floating-point values across preemptions.
    volatile double fp = 1.0;

    time_t start = time(nullptr);
    uint64_t iters = 0;
    while (time(nullptr) - start < SPIN_SECONDS) {
        for (int i = 0; i < 20000; ++i) {
            uint64_t v = mix(g_data_accum ^ iters ^ i);
            g_bss_accum = g_bss_accum + v;   // .bss
            bss_local   = bss_local + v;     // stack shadow of .bss
            stack_shadow = mix(stack_shadow ^ v);
            g_data_accum = mix(g_data_accum ^ stack_shadow);
            fp = fp * 1.0000001 + 0.5;       // keep FPU/SSE live
            if (fp > 1e18) fp = 1.0;
        }
        // Make syscalls and touch fresh COW pages while being preempted, like
        // the checkpointer.  mmap a page, fault it in (COW demand fault), write
        // it, unmap it -- driving the vm_fault path under preemption.
        void *p = mmap(nullptr, 65536, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p != MAP_FAILED) {
            memset(p, (int)iters, 65536);    // fault in + touch every page
            munmap(p, 65536);
        }
        // A couple of real syscalls to exercise the syscall-stack + preemption
        // interplay.
        (void)getpid();
        (void)sched_yield();
        ++iters;
        // Verify the two lockstep accumulators still agree at every outer step.
        if (g_bss_accum != bss_local) {
            return CHILD_MISMATCH;
        }
    }
    (void)fp;
    // Final consistency check: the .bss accumulator and its stack shadow must
    // still be identical after thousands of preemptions.
    return (g_bss_accum == bss_local) ? CHILD_OK : CHILD_MISMATCH;
}

// Recurse to build a deep stack, then spin at the leaf.  The `guard` locals at
// every level must survive all the preemptions unchanged -- a corrupt resume
// that lands with a wild stack pointer typically smashes one of them.
static int child_recurse(int depth, uint64_t guard)
{
    volatile uint64_t local_guard = guard ^ (0xa5a5a5a5ULL * depth);
    int rc;
    if (depth <= 0) {
        rc = child_spin_leaf();
    } else {
        rc = child_recurse(depth - 1, local_guard);
    }
    // On the way back up, verify our frame's guard is intact.
    if (local_guard != (guard ^ (0xa5a5a5a5ULL * depth))) {
        return CHILD_MISMATCH;
    }
    return rc;
}

int main()
{
    printf("=== tst-fork-preempt ===\n");

    pid_t pid = fork();
    if (pid == 0) {
        // CHILD: spin under preemption, verifying its own context integrity,
        // deep in a nested same-VA stack.
        _exit(child_recurse(RECURSE_DEPTH, 0xdeadbeefULL));
    }
    CHECK(pid > 0, "fork() returned child pid to parent");
    if (pid <= 0) {
        printf("=== tst-fork-preempt done: %d failures ===\n", failures);
        return 1;
    }

    // PARENT: stay runnable and compete for the CPU so the scheduler keeps
    // preempting the child in and out of its same-VA stack.  Do real work
    // (don't just sleep) so on a single CPU the child is genuinely preempted.
    {
        volatile uint64_t junk = 1;
        time_t start = time(nullptr);
        while (time(nullptr) - start < SPIN_SECONDS) {
            for (int i = 0; i < 20000; ++i) {
                junk = mix(junk ^ i);
            }
        }
        (void)junk;
    }

    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    CHECK(w == pid, "waitpid() reaped the heavily-preempted fork child");
    CHECK(WIFEXITED(status),
          "child exited normally (no SIGSEGV from a corrupt preemptive switch)");
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == CHILD_OK,
          "child's globals + stack stayed self-consistent across many preemptions");

    printf("=== tst-fork-preempt done: %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
