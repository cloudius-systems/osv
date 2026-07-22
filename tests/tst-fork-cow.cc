/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Stage 2 fork() proof: per-child address space with copy-on-write.
 *
 * After fork() the child must get a logically INDEPENDENT copy of every
 * PRIVATE mapping (this test uses a writable global in .data), while MAP_SHARED
 * mappings stay truly SHARED between parent and child.  We verify:
 *   1. the child writing a distinct value to a private global does NOT change
 *      the parent's copy (COW isolation), and
 *   2. the child writing to a MAP_SHARED region IS visible in the parent
 *      (sharing preserved).
 * This is exactly the contract stock multi-process PostgreSQL relies on
 * (private backend heap/globals, shared shared_buffers).
 */

#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

// A writable global lives in the program's PRIVATE .data mapping.  Under COW
// fork the child gets its own copy; the parent's value must survive the child
// mutating it.
static volatile int private_global = 0x1111;

// The MAP_SHARED region pointer, kept in a global so accessing it does not
// depend on the (biased) stack in the current fork-thread model.
static volatile int *shared_ptr;

int main()
{
    printf("=== tst-fork-cow ===\n");

    // A MAP_SHARED anonymous region: parent and child map the SAME physical
    // pages, so writes are mutually visible (not COW).
    volatile int *shared = (volatile int*)mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    CHECK(shared != MAP_FAILED, "mmap MAP_SHARED|MAP_ANONYMOUS region");
    if (shared == (volatile int*)MAP_FAILED) {
        printf("=== tst-fork-cow done: %d failures ===\n", failures);
        return 1;
    }
    shared_ptr = shared;
    shared[0] = 0xAAAA;      // parent's initial value in the shared region

    pid_t pid = fork();
    if (pid == 0) {
        // CHILD: mutate BOTH the private global and the shared region.  Use the
        // global shared_ptr (not a stack local) to avoid any stack-copy bias.
        private_global = 0x2222;         // must stay private to the child
        shared_ptr[0]  = 0xBBBB;         // must be visible to the parent
        // Read back locally to be sure the writes landed in the child.
        _exit((private_global == 0x2222 && shared_ptr[0] == 0xBBBB) ? 0 : 1);
    }

    CHECK(pid > 0, "fork() returned child pid to parent");
    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    CHECK(w == pid, "waitpid() reaped the fork child");
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "child observed its own private + shared writes");

    // THE PROOF:
    // (1) COW isolation: the parent's private global is UNCHANGED even though
    //     the child set it to 0x2222.
    CHECK(private_global == 0x1111,
          "COW: parent's private global unchanged by child's write");
    // (2) Sharing preserved: the child's write to the MAP_SHARED region IS seen
    //     by the parent.
    CHECK(shared[0] == 0xBBBB,
          "SHARED: child's write to MAP_SHARED region visible in parent");

    munmap((void*)shared, 4096);
    printf("=== tst-fork-cow done: %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
