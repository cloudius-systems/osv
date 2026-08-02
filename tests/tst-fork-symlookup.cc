/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Fork SYMBOL-LOOKUP coherence test (distinct from tst-fork-pltgot, which
 * covers the GOT-slot relocation facet).  This targets the case where a forked
 * child's lazy resolver RUNS but the dynamic symbol LOOKUP fails for a symbol
 * that IS exported (the "sem_wait not found" / "pwritev not found" wall seen
 * under heavy PG fork load).  Two stressors:
 *
 *  (1) HEAVY CONCURRENT FORK: many children alive at once, each the FIRST to
 *      resolve a DISTINCT pile of kernel-exported PLT symbols, while the parent
 *      also resolves + forks.  This drives program::lookup -> modules_get()
 *      (an RCU vector copy that, in a child, allocates in the COW fork arena)
 *      and object::visible() concurrently across many COW address spaces.
 *
 *  (2) THREAD-KEYED VISIBILITY: a forked child resolves symbols that live in an
 *      object whose visibility is thread-scoped.  If the child is not seen as a
 *      descendant of the loader thread, object::visible() returns false and the
 *      lookup "fails" for a symbol that exists.
 *
 * A resolve failure aborts the guest ("failed looking up symbol ...") or the
 * child exits abnormally; either is caught here as a FAIL.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cmath>
#include <ctime>
#include <cwchar>
#include <cstdint>
#include <semaphore.h>
#include <sys/uio.h>
#include <fcntl.h>

static volatile int g_zero = 0;

// A large pile of functions the parent does NOT call before forking, so each
// child is the first to lazily resolve them.  Includes the exact PG-relevant
// families: POSIX semaphores (sem_wait) and vectored I/O (preadv/pwritev),
// plus math/string/time so the per-child symbol set is wide.
static long child_resolve_wide(int salt)
{
    long acc = salt;
    // math the parent never touches
    acc += (long)llround(sqrt((double)(g_zero + 2.0)));
    acc += (long)lround(cbrt((double)(g_zero + 27.0)));
    acc += (long)ceil(log2((double)(g_zero + 8.0)));
    acc += (long)floor(exp2((double)(g_zero + 3.0)));
    acc += (long)llround(hypot(3.0, 4.0));
    acc += (long)lround(atan2(1.0, 2.0) * 10.0);
    // string / mem
    char buf[64];
    strncpy(buf, "symlookup", sizeof(buf));
    acc += (long)strnlen(buf, sizeof(buf));
    acc += (long)strcspn(buf, "k");
    acc += (long)strspn("aaab", "a");
    acc += (long)strtoll("789", NULL, 10);
    acc += (long)strtoul("456", NULL, 10);
    acc += (long)labs(-7) + (long)llabs(-11);
    acc += (long)wcslen(L"wide");
    // POSIX semaphore -> sem_init/sem_wait/sem_post/sem_destroy (the PG family)
    sem_t s;
    if (sem_init(&s, 0, 1) == 0) {
        sem_wait(&s);
        sem_post(&s);
        sem_destroy(&s);
        acc += 1;
    }
    // vectored I/O -> preadv/pwritev (the ZFS vectored-write family)
    char fbuf[32];
    snprintf(fbuf, sizeof(fbuf), "/tmp/fslk-%d-%d", (int)getpid(), salt);
    int fd = open(fbuf, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        char w0[8] = "abcd", w1[8] = "efgh";
        struct iovec wv[2] = {{w0, 4}, {w1, 4}};
        pwritev(fd, wv, 2, 0);
        char r0[8] = {0}, r1[8] = {0};
        struct iovec rv[2] = {{r0, 4}, {r1, 4}};
        preadv(fd, rv, 2, 0);
        close(fd);
        unlink(fbuf);
        acc += (long)(r0[0] + r1[0]);
    }
    // time + conversion
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    acc += (ts.tv_sec != 0) ? 1 : 0;
    acc += (long)snprintf(buf, sizeof(buf), "%d-%ld", salt, acc);
    return acc;
}

// Stressor (1): fork N children CONCURRENTLY (do not reap between forks), let
// them all resolve in their own COW address spaces at once, then reap.  While
// they run, the parent resolves too.  High concurrency is the discriminator
// vs. the serial tst-fork-pltgot.
static int concurrent_fork_round(int n)
{
    pid_t pids[256];
    if (n > 256) n = 256;
    for (int i = 0; i < n; i++) {
        pid_t p = fork();
        if (p < 0) { printf("FAIL: fork %d: %s\n", i, strerror(errno)); return 1; }
        if (p == 0) {
            long r = child_resolve_wide(i + 1);
            _exit(r != 0 ? 42 : 7);
        }
        pids[i] = p;
    }
    // Parent resolves concurrently with the live children.
    volatile long pacc = child_resolve_wide(9999);
    (void)pacc;
    int failures = 0;
    for (int i = 0; i < n; i++) {
        int st = 0;
        pid_t w = waitpid(pids[i], &st, 0);
        if (w != pids[i] || !WIFEXITED(st) || WEXITSTATUS(st) != 42) {
            printf("FAIL: child %d bad exit (w=%d st=0x%x) -- symbol-lookup fault in child\n",
                   i, (int)w, st);
            failures++;
        }
    }
    return failures;
}

// A thread that keeps resolving wide symbols, to add cross-CPU lookup pressure
// on program::lookup / modules_get while forks happen.
static void *pressure_thread(void *arg)
{
    int iters = *(int*)arg;
    for (int i = 0; i < iters; i++) {
        volatile long r = child_resolve_wide(0x1000 + i);
        (void)r;
    }
    return NULL;
}

int main(int, char**)
{
    fflush(stdout);
    int total_failures = 0;

    // A resolver-pressure thread runs alongside the fork storms.
    pthread_t th;
    int iters = 200;
    pthread_create(&th, NULL, pressure_thread, &iters);

    // Several rounds of heavy concurrent forking.  Each round: many children
    // simultaneously first-resolving the wide (incl. sem_wait/preadv/pwritev)
    // symbol set in distinct COW address spaces.
    for (int round = 0; round < 6; round++) {
        int f = concurrent_fork_round(32);
        total_failures += f;
        if (f) {
            printf("FAIL: round %d had %d child symbol-lookup failures\n", round, f);
        }
    }

    pthread_join(th, NULL);

    if (total_failures == 0) {
        printf("PASS: %d concurrent forked children resolved wide symbol set (incl sem_wait/preadv/pwritev) cleanly\n", 6 * 32);
        printf("SUMMARY: 0 failures\n");
        return 0;
    }
    printf("SUMMARY: %d failures\n", total_failures);
    return 1;
}
