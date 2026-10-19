/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * REGRESSION TEST for [W-catalog-read]: the multi-backend MAP_SHARED
 * catalog-READ zero-page wall that blocks stock PostgreSQL from serving a
 * SECOND concurrent connection on OSv.
 *
 * PG's shared_buffers (shared_memory_type=mmap, the default) is one big
 * anonymous MAP_SHARED segment created by the postmaster BEFORE it forks any
 * backend.  Only a FEW of its pages are touched at postmaster start; the vast
 * majority (holding the system-catalog INDEX pages, relcache, etc.) are
 * first-faulted by a BACKEND, post-fork.  The observed failure:
 *
 *   backend #1 (a forked child) reads a catalog-index page -> pages it in fine;
 *   backend #2+ (SIBLING forked children, no writes in between) read the SAME
 *     shared page -> get ZEROS:
 *       "pg_authid_rolname_index contains unexpected zero page at block N"
 *       "cache lookup failed".
 *
 * ROOT CAUSE (fork COW / anon-shared fault path, core/mmu.cc): an anonymous
 * MAP_SHARED page NOT yet present in the parent's page table at fork time is
 * cloned as an EMPTY child PTE (clone_pt_level0 has nothing to share).  When a
 * sibling backend later faults that VA, the anon page provider
 * (initialized_anonymous_page_provider::map) calls memory::alloc_page() and
 * installs a FRESH, PRIVATE, ZERO page in that backend's address space only.
 * Every sibling that first-touches the page after its own fork gets a
 * different private zero page -> they never converge on the ONE shared
 * physical page MAP_SHARED promises.  So the page backend #1 populated is
 * invisible to siblings B, C, ... which read zeros.
 *
 * This mirrors PG exactly: a shared_buffers/catalog page first-touched by one
 * backend must be the SAME physical page in every sibling backend.
 *
 * The test:
 *   parent creates an anonymous MAP_SHARED region and forks THREE children up
 *   front (A, B, C) -- all forked BEFORE any of the test pages are touched, so
 *   each child's clone has EMPTY PTEs for them (the PG ordering: postmaster
 *   forks backends before backends fault catalog pages).
 *   A control page (touched by the parent before fork, so it IS shared) carries
 *   a step counter used to serialize:  A writes a distinctive pattern into the
 *   payload pages, bumps the counter; B and C then READ the payload pages and
 *   must see A's pattern (NOT zeros).  Each child owns its verdict via exit
 *   code; the parent waitpid-verifies all three and also reads the pages.
 *
 * On HEAD (the bug) B and C read zeros -> FAIL.  With the fix (a fault in a
 * child that resolves an anon-MAP_SHARED page to the ONE shared backing page)
 * B and C see A's pattern -> PASS.
 *
 * To reproduce (arena-dev, CONF_fork=y), boot with -smp 2 AND -smp 4 (it is a
 * coherence bug -- exercise real concurrency, not gdb-serialized CPUs):
 *   ./scripts/build conf_fork=1 fs=ramfs image=tests
 *   qemu ... --rootfs=ramfs /tests/tst-fork-shared-catalog.so  (-smp 2, -smp 4)
 */

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cstdint>

// Several pages so we exercise more than one leaf PTE / a whole PT subtree.
static const size_t NPAGES = 16;
static const size_t PAGE   = 4096;
// Payload region: the "catalog pages" A populates and B/C must read coherently.
// Region 0 = control page (parent touches it pre-fork -> genuinely shared);
// regions 1..NPAGES = payload pages (untouched pre-fork -> empty child PTEs).
static const size_t REGION = (NPAGES + 1) * PAGE;

// Control-page layout (page 0 of the region).
struct control {
    volatile int a_done;    // A sets 1 after writing the payload
    volatile int b_ok;      // B sets 1 if it saw A's pattern
    volatile int c_ok;      // C sets 1 if it saw A's pattern
};

// Deterministic per-byte pattern so a reader detects both a zero page (the bug)
// and any silent divergence (a private, incoherent copy).
static inline uint8_t pat(size_t off) {
    return (uint8_t)((off * 2654435761u + 40503u) >> 15);
}

static uint8_t *payload(uint8_t *base, size_t page) {
    return base + (page + 1) * PAGE;   // page 0 is the control page
}

static void write_payload(uint8_t *base) {
    for (size_t p = 0; p < NPAGES; p++) {
        uint8_t *pg = payload(base, p);
        for (size_t i = 0; i < PAGE; i++) pg[i] = pat(p * PAGE + i);
    }
}

// Returns 0 if all payload pages match A's pattern, else (failing global off)+1.
static size_t verify_payload(uint8_t *base) {
    for (size_t p = 0; p < NPAGES; p++) {
        uint8_t *pg = payload(base, p);
        for (size_t i = 0; i < PAGE; i++) {
            if (pg[i] != pat(p * PAGE + i)) return p * PAGE + i + 1;
        }
    }
    return 0;
}

// Spin-wait on a shared flag with a timeout (ms).  Returns true if it went set.
static bool wait_flag(volatile int *flag, int timeout_ms) {
    for (int i = 0; i < timeout_ms; i++) {
        __sync_synchronize();
        if (*flag) return true;
        usleep(1000);
    }
    return false;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== tst-fork-shared-catalog ===\n");

    // Anonymous MAP_SHARED region -- exactly PG shared_buffers with the default
    // shared_memory_type=mmap.  Created by the parent (the postmaster).
    uint8_t *base = (uint8_t *)mmap(nullptr, REGION, PROT_READ | PROT_WRITE,
                                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        printf("FAIL: mmap(MAP_SHARED|MAP_ANONYMOUS) errno=%d\n", errno);
        return 1;
    }
    // Touch ONLY the control page before forking (so it is present + shared in
    // every child, giving us a coherent way to serialize).  The payload pages
    // are deliberately left untouched -> empty child PTEs, the PG ordering.
    struct control *ctl = (struct control *)base;
    ctl->a_done = 0;
    ctl->b_ok = 0;
    ctl->c_ok = 0;
    __sync_synchronize();

    // Fork all THREE children up front, BEFORE any payload page is touched.
    // Each is a sibling of the others (all children of the parent), matching
    // sibling PG backends forked by the postmaster.
    pid_t a = fork();
    if (a == 0) {
        // Child A = the FIRST backend that pages the shared catalog in.
        write_payload(base);
        __sync_synchronize();
        ctl->a_done = 1;
        __sync_synchronize();
        // A also self-verifies (always works within its own AS).
        if (verify_payload(base) != 0) { printf("FAIL(A): self-verify\n"); _exit(2); }
        printf("child A: wrote+verified %zu payload pages\n", NPAGES);
        _exit(0);
    }
    if (a < 0) { printf("FAIL: fork A errno=%d\n", errno); return 1; }

    pid_t b = fork();
    if (b == 0) {
        // Child B = a SIBLING backend #2 that reads the shared pages A wrote.
        if (!wait_flag(&ctl->a_done, 10000)) { printf("FAIL(B): timeout waiting for A\n"); _exit(3); }
        size_t bad = verify_payload(base);
        if (bad != 0) {
            printf("FAIL(B): mismatch/zero at global off %zu -- sibling read a "
                   "PRIVATE zero page, not A's shared write\n", bad - 1);
            _exit(4);
        }
        __sync_synchronize();
        ctl->b_ok = 1;
        __sync_synchronize();
        printf("child B: read back %zu payload pages, all match A's pattern\n", NPAGES);
        _exit(0);
    }
    if (b < 0) { printf("FAIL: fork B errno=%d\n", errno); return 1; }

    pid_t c = fork();
    if (c == 0) {
        // Child C = a SIBLING backend #3 that reads the same shared pages.
        if (!wait_flag(&ctl->a_done, 10000)) { printf("FAIL(C): timeout waiting for A\n"); _exit(3); }
        size_t bad = verify_payload(base);
        if (bad != 0) {
            printf("FAIL(C): mismatch/zero at global off %zu -- sibling read a "
                   "PRIVATE zero page, not A's shared write\n", bad - 1);
            _exit(4);
        }
        __sync_synchronize();
        ctl->c_ok = 1;
        __sync_synchronize();
        printf("child C: read back %zu payload pages, all match A's pattern\n", NPAGES);
        _exit(0);
    }
    if (c < 0) { printf("FAIL: fork C errno=%d\n", errno); return 1; }

    int rc = 0, st = 0;
    struct { pid_t pid; const char *name; } kids[] = {{a,"A"},{b,"B"},{c,"C"}};
    for (auto &k : kids) {
        if (waitpid(k.pid, &st, 0) != k.pid || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            printf("FAIL: child %s exit status=0x%x\n", k.name, st);
            rc = 1;
        }
    }
    // Parent (AS0) also reads the payload back -- it too first-touches these
    // pages post-fork and must resolve them to A's shared write.
    if (verify_payload(base) != 0) {
        printf("FAIL: parent read-back mismatch/zero -- AS0 saw a private zero page\n");
        rc = 1;
    }
    if (rc == 0) {
        printf("PASS: an anonymous MAP_SHARED page first-touched by one forked "
               "backend is coherent in all sibling backends and the parent "
               "(multi-backend shared-catalog read coherence)\n");
        printf("tst-fork-shared-catalog done: 0 failures\n");
    }
    munmap(base, REGION);
    return rc;
}
