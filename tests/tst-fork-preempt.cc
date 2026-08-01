/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Regression test for a LONG-LIVED, heavily-PREEMPTED fork child running on the
 * parent's same-VA stack (the PostgreSQL-checkpointer profile), which the
 * existing fork tests never exercised: they fork short-lived children preempted
 * only a handful of times.  See documentation/fork.md.
 *
 * A fork child spins for several seconds deep in a nested same-VA stack,
 * touching .data/.bss globals, its own stack and the FPU, while the parent
 * competes for the CPU -- so the child is preempted hundreds of times.  It
 * keeps a stack-local checksum in lockstep with a .bss checksum and exits with
 * a known code the parent verifies.  This proves the preemptive context switch
 * preserves a fork child's register/stack context across many switches.
 *
 * NOTE on the separately-found wall: a fork child that calls mmap() AFTER fork
 * and then writes the region SIGSEGVs, because on this branch only the page-
 * FAULT path is per-child-address-space aware (core/mmu.cc vm_fault consults
 * as->vmas), while the mmap/munmap ALLOCATION path (mmu::allocate / map_anon
 * via the global vma_list + vma_range_set) is NOT -- so a child's new mapping
 * lands in the GLOBAL list, invisible to the child's own fault handler.  That
 * bug is NOT exercised here because a fork child's mmap+write faults with a
 * KERNEL pc (rep-stos) and hits abort() in vm_sigsegv, taking down the whole
 * unikernel (it cannot be contained in-process).  See tst-fork-child-mmap.cc
 * (standalone repro, not in the suite) and /tmp/pg-preempt-fix.txt.
 */

#include <unistd.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <cstdio>
#include <cstdint>
#include <ctime>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

// Globals in .data and .bss the child mutates while being preempted.  If a
// preemptive switch corrupts the child's context these diverge from the
// stack-local shadow, or the child crashes before it can compare them.
static volatile uint64_t g_data_accum = 0x0123456789abcdefULL;  // .data
static volatile uint64_t g_bss_accum;                           // .bss (zero)

enum { CHILD_OK = 55, CHILD_MISMATCH = 66 };

// Long enough to be preempted many hundreds of times while the parent competes.
static const int SPIN_SECONDS = 4;
// Deep same-VA stack so preemption catches the child in nested frames, like the
// checkpointer spinning inside InitAuxiliaryProcess / CheckpointerMain.
static const int RECURSE_DEPTH = 64;

static uint64_t mix(uint64_t x)
{
    // Order-sensitive splitmix64-style scramble: a dropped/duplicated iteration
    // from a corrupt resume changes the result.  Nothing crypto.
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static int child_spin_leaf()
{
    volatile uint64_t stack_shadow = 0;
    volatile uint64_t bss_local = 0;
    volatile double fp = 1.0;   // keep FPU/SSE live across preemptions

    time_t start = time(nullptr);
    uint64_t iters = 0;
    while (time(nullptr) - start < SPIN_SECONDS) {
        for (int i = 0; i < 20000; ++i) {
            uint64_t v = mix(g_data_accum ^ iters ^ i);
            g_bss_accum = g_bss_accum + v;   // .bss
            bss_local   = bss_local + v;     // stack shadow of .bss
            stack_shadow = mix(stack_shadow ^ v);
            g_data_accum = mix(g_data_accum ^ stack_shadow);
            fp = fp * 1.0000001 + 0.5;
            if (fp > 1e18) fp = 1.0;
        }
        // Real syscalls to exercise the syscall-stack + preemption interplay.
        (void)getpid();
        (void)sched_yield();
        ++iters;
        if (g_bss_accum != bss_local) {
            return CHILD_MISMATCH;
        }
    }
    (void)fp;
    return (g_bss_accum == bss_local) ? CHILD_OK : CHILD_MISMATCH;
}

// Recurse to build a deep stack, then spin at the leaf.  Each frame's guard
// must survive every preemption unchanged -- a corrupt resume that lands with a
// wild stack pointer typically smashes one of them.
static int child_recurse(int depth, uint64_t guard)
{
    volatile uint64_t local_guard = guard ^ (0xa5a5a5a5ULL * depth);
    int rc = (depth <= 0) ? child_spin_leaf() : child_recurse(depth - 1, local_guard);
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
    // preempting the child in and out of its same-VA stack (real work, no sleep).
    volatile uint64_t junk = 1;
    time_t start = time(nullptr);
    while (time(nullptr) - start < SPIN_SECONDS) {
        for (int i = 0; i < 20000; ++i) {
            junk = mix(junk ^ i);
        }
    }
    (void)junk;

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
