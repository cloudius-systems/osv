/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Fork heap-isolation microtest (the unit proof behind CONF_fork + the fork
 * heap arena).
 *
 * PostgreSQL is fork-per-backend: the postmaster builds heap state (palloc'd
 * structures, catalog caches, ...) BEFORE it forks a backend, and the backend
 * then WRITES to those inherited heap objects.  On a normal OSv the small-
 * object malloc heap lives in the kernel IDENTITY MAP, which is shared verbatim
 * across every address space and cannot be copy-on-write cloned -- so a forked
 * child scribbling an inherited heap object corrupts the parent's heap.
 *
 * With the fork heap arena, application malloc comes from an app-slot COW-able
 * region, so a child's heap writes are private.  This test proves it three
 * ways:
 *   1. INHERITED small heap object: parent mallocs+initializes it before fork;
 *      the child overwrites it; the parent must NOT see the child's value.
 *   2. NEW child small malloc: the child allocates+writes after fork; the value
 *      is correct in the child and the parent is unaffected.
 *   3. A larger (multi-KB) inherited heap object is likewise isolated.
 *
 * Without the arena (small heap in the identity map) case 1 FAILS (the parent
 * sees the child's write); with the arena it PASSES.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

/* A MAP_SHARED region so the child can report the value it observed for the
 * inherited object back to the parent without relying on exit status alone. */
static volatile int *shared_report;

int main()
{
    printf("=== tst-pgfork (heap isolation) ===\n");

    /* Parent allocates and initializes small + large heap objects BEFORE fork,
     * exactly as the postmaster builds pre-fork heap state. */
    int *small = (int *)malloc(sizeof(int));
    CHECK(small != NULL, "parent malloc small heap object");
    *small = 0x1111;

    const size_t big_n = 2048;               /* multi-KB inherited object */
    unsigned char *big = (unsigned char *)malloc(big_n);
    CHECK(big != NULL, "parent malloc large heap object");
    memset(big, 0xAA, big_n);

    shared_report = (volatile int *)mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    CHECK(shared_report != MAP_FAILED, "mmap shared report region");
    if (shared_report == (volatile int *)MAP_FAILED) {
        printf("=== tst-pgfork done: %d failures ===\n", failures);
        return 1;
    }
    shared_report[0] = 0;

    pid_t pid = fork();
    if (pid == 0) {
        /* CHILD: mutate the INHERITED heap objects and allocate a NEW one. */
        *small = 0x2222;
        memset(big, 0xBB, big_n);

        int *child_new = (int *)malloc(sizeof(int));
        int new_ok = 0;
        if (child_new) {
            *child_new = 0x3333;
            new_ok = (*child_new == 0x3333);
            free(child_new);
        }

        /* Report what the child sees so the parent can sanity-check the child
         * side actually wrote through. */
        shared_report[0] = *small;

        int child_ok = (*small == 0x2222) && (big[0] == 0xBB) &&
                       (big[big_n - 1] == 0xBB) && new_ok;
        _exit(child_ok ? 0 : 1);
    }

    CHECK(pid > 0, "fork() returned child pid to parent");
    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    CHECK(w == pid, "waitpid() reaped the fork child");
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "child wrote+read its own heap copies");
    CHECK(shared_report[0] == 0x2222,
          "child observed its own inherited-heap write (0x2222)");

    /* THE PROOF: the parent's inherited heap objects are UNCHANGED, even though
     * the child overwrote them.  This is the isolation stock fork-per-backend
     * PostgreSQL relies on. */
    CHECK(*small == 0x1111,
          "HEAP COW: parent's small inherited object unchanged by child");
    CHECK(big[0] == 0xAA && big[big_n - 1] == 0xAA,
          "HEAP COW: parent's large inherited object unchanged by child");

    free(small);
    free(big);
    munmap((void *)shared_report, 4096);
    printf("=== tst-pgfork done: %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
