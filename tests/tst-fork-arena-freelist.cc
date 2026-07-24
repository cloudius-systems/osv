/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Regression test for the fork-arena free-list-across-COW-boundary corruption
 * (the PostgreSQL walwriter/checkpointer AllocSetReset wall).
 *
 * The fork arena (core/fork_arena.cc) carves each chunk a globally-unique VA
 * off a shared bump pointer, but on HEAD it kept the segregated free-list HEADS
 * in kernel BSS -- the identity map, shared VERBATIM across every fork address
 * space and NEVER COW-cloned.  The chunks (and their free-list links) are
 * COW-private per child.  The bug: a chunk B the PARENT frees goes on the
 * SHARED free-list, so a CHILD can pop and reuse B -- even though the child
 * inherited B *live* (B is still referenced by a data structure the child
 * COW-inherited).  The child then overwrites B with fresh data while its own
 * inherited structure still points at B.  PostgreSQL hit exactly this: a
 * checkpointer/walwriter memory-context block, inherited live from the
 * postmaster, was re-handed to the aux process off the shared free-list for a
 * path string; walking the context's block list in AllocSetReset then read a
 * `next` clobbered with "...pid..." and SIGSEGV'd.
 *
 * This test reproduces the ownership cross-over deterministically.  The parent
 * allocates a set of chunks B[], records their addresses in a COW-inherited
 * array, and STAMPS each with a self-describing sentinel -- these are the
 * "live, inherited" chunks.  It forks.  The parent then FREES all of B[] (onto
 * the shared free-list) and hammers alloc+scribble of the same size class so
 * those freed chunks get re-handed and overwritten.  The child, meanwhile, does
 * NOT free B[] -- they are live to it -- but it ALSO allocates the same size
 * class (as PG's aux process does), which on HEAD pops B[] chunks off the
 * shared free-list into the child, and the child scribbles them.  Either way,
 * the child then verifies B[] still hold their sentinels.  With the shared
 * free-list, a B[] chunk was re-handed (to parent or child) and overwritten ->
 * the child's inherited B[] sentinel is gone -> CHILD_CORRUPT.  With a per-AS
 * free-list, the parent's frees stay on the parent's list and the child only
 * ever pops its OWN freed chunks, so the inherited B[] are never disturbed.
 *
 * On HEAD (shared free-list): child sees a clobbered inherited chunk -> FAIL.
 * With the fix (per-AS free-list): inherited chunks stay intact -> PASS.
 */

#include <unistd.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

enum { CHILD_OK = 55, CHILD_CORRUPT = 66 };

static const size_t CHUNK = 200;    // PG memory-context block size class
static const int    NLIVE = 256;    // live, inherited chunks
static const int    ROUNDS = 4000;

static const uint64_t LIVE_TAG   = 0x11ee11ee11ee11eeULL;
static const uint64_t SCRIB_TAG  = 0xddddddddddddddddULL;

static void stamp(void *p, uint64_t tag)
{
    uint64_t v = tag ^ reinterpret_cast<uintptr_t>(p);
    uint64_t *q = static_cast<uint64_t*>(p);
    for (size_t i = 0; i < CHUNK / sizeof(uint64_t); ++i) q[i] = v;
}
static bool verify(void *p, uint64_t tag)
{
    uint64_t v = tag ^ reinterpret_cast<uintptr_t>(p);
    uint64_t *q = static_cast<uint64_t*>(p);
    for (size_t i = 0; i < CHUNK / sizeof(uint64_t); ++i)
        if (q[i] != v) return false;
    return true;
}

// COW-inherited array of the live chunk pointers (in .bss, cloned per child).
static void *g_live[NLIVE];

int main()
{
    printf("=== tst-fork-arena-freelist ===\n");

    // Allocate the "live, inherited" chunks and stamp them.
    for (int i = 0; i < NLIVE; ++i) {
        g_live[i] = malloc(CHUNK);
        stamp(g_live[i], LIVE_TAG);
    }
    CHECK(g_live[0] != nullptr, "allocated live inherited chunks");

    pid_t pid = fork();
    if (pid == 0) {
        // CHILD: the g_live chunks are live to it (COW-inherited).  It also
        // allocates the same size class -- on HEAD that pops g_live chunks the
        // parent freed off the SHARED free-list into the child, and the child
        // scribbles them.  Then it checks its inherited g_live are still intact.
        for (int r = 0; r < ROUNDS; ++r) {
            void *tmp[64];
            for (int i = 0; i < 64; ++i) {
                tmp[i] = malloc(CHUNK);
                if (!tmp[i]) _exit(CHILD_CORRUPT);
                stamp(tmp[i], SCRIB_TAG);
            }
            for (int i = 0; i < 64; ++i) free(tmp[i]);
            // The inherited live chunks must never have been re-handed/overwritten.
            for (int i = 0; i < NLIVE; ++i) {
                if (!verify(g_live[i], LIVE_TAG)) _exit(CHILD_CORRUPT);
            }
            sched_yield();
        }
        _exit(CHILD_OK);
    }
    CHECK(pid > 0, "fork() returned child pid to parent");

    // PARENT: free the live chunks (onto the shared free-list on HEAD) and
    // hammer alloc+scribble so they get re-handed and overwritten.
    for (int i = 0; i < NLIVE; ++i) free(g_live[i]);
    for (int r = 0; r < ROUNDS * 4; ++r) {
        void *p[64];
        for (int i = 0; i < 64; ++i) {
            p[i] = malloc(CHUNK);
            stamp(p[i], SCRIB_TAG);
        }
        for (int i = 0; i < 64; ++i) free(p[i]);
        sched_yield();
    }

    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    CHECK(w == pid, "waitpid reaped the child");
    CHECK(WIFEXITED(status), "child exited normally (did not crash)");
    if (WIFEXITED(status)) {
        CHECK(WEXITSTATUS(status) == CHILD_OK,
              "child's inherited-live arena chunks were never re-handed across the COW boundary");
        if (WEXITSTATUS(status) == CHILD_CORRUPT)
            printf("  child saw a cross-address-space free-list re-hand of a live chunk\n");
    }

    printf("=== tst-fork-arena-freelist done: %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
