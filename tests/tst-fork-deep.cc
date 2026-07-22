/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Deep-call-chain fork() test.  fork() is called from several nested function
 * calls deep; the child must return ALL the way back up the call chain (popping
 * each frame correctly) and _exit(N).  The parent waitpid()s and checks N.
 *
 * This exercises the stack-fidelity of fork(): saved return addresses AND saved
 * frame pointers (rbp) and any stack-internal pointers must remain valid in the
 * child as it unwinds.  A fork() that relocates the child stack to a different
 * virtual address and biases the SP leaves saved rbp/&local pointing at the
 * PARENT stack (off by the bias), which corrupts deep unwinds -- shallow
 * callers (tst-fork) happen to survive, deep ones do not.  This test is the one
 * that catches that; it passes only when the child keeps the parent's exact
 * stack addresses (same-VA COW stack).
 */

#include <unistd.h>
#include <sys/wait.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

// A chain of nested calls.  Each frame has a local the compiler may address
// relative to rbp, and passes a running sum by pointer (a stack-internal
// pointer) to the next frame -- both must survive the fork in the child.
static pid_t g_child;

// Returns the depth-accumulated value the child computed as it unwound, or the
// child pid in the parent.  `depth` counts down; `acc` sums the depths.
static long deep(int depth, long acc, int *checkptr)
{
    volatile long marker = 0xC0DE0000L + depth;   // rbp-relative local
    int here = depth;                              // &here is a stack pointer
    if (depth == 0) {
        // Bottom of the chain: fork here, deep in the call stack.
        pid_t pid = fork();
        if (pid == 0) {
            // CHILD: must now return up through all `deep` frames.  If any
            // saved rbp/return-addr/&local is off, this unwind corrupts.
            // We return acc and let the frames add their markers back.
            return acc;   // child returns acc up the chain
        }
        g_child = pid;
        return -1;        // parent sentinel
    }
    long r = deep(depth - 1, acc + depth, checkptr);
    // On the way back up, verify our own frame survived: marker and here must
    // still hold what we set before the recursive call.
    if (marker != (0xC0DE0000L + depth) || here != depth) {
        *checkptr = 1;    // frame corruption detected
    }
    if (r < 0) return -1; // propagate parent sentinel
    return r + depth;     // child accumulates on the way up
}

int main()
{
    printf("=== tst-fork-deep ===\n");
    const int N = 12;                 // 12 frames deep
    // Expected child return: acc built going down = sum(1..N); then on the way
    // up each frame adds its depth again => acc + sum(1..N) = 2*sum(1..N).
    // acc going down = N + (N-1) + ... + 1 = N*(N+1)/2.  Up adds the same.
    long expected = (long)N * (N + 1);   // 2 * N*(N+1)/2
    int corrupt = 0;

    long r = deep(N, 0, &corrupt);
    if (r >= 0) {
        // We are the CHILD (it returned a non-negative accumulated value and
        // unwound all the way here).  Exit with a status encoding success.
        int ok = (r == expected && corrupt == 0);
        _exit(ok ? 77 : 66);
    }

    // PARENT: r < 0 sentinel.  g_child holds the child pid.
    CHECK(g_child > 0, "fork() deep in the call chain returned child pid");
    int status = 0;
    pid_t w = waitpid(g_child, &status, 0);
    CHECK(w == g_child, "waitpid() reaped the deep-fork child");
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 77,
          "child unwound the deep call chain correctly (frames intact)");
    if (WIFEXITED(status) && WEXITSTATUS(status) == 66) {
        printf("NOTE: child unwound but computed the wrong value / detected "
               "frame corruption -> stack-bias bug present.\n");
    }

    printf("=== tst-fork-deep done: %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
