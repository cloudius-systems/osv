/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A long-lived fork child whose heap rises and falls must not grow.  The
// child runs 200 rounds; each allocates 5000 chunks of mixed size classes,
// writes them and frees them all.  Every round makes the same requests, so
// from round 2 on every one can reuse a chunk freed in the round before, and
// the memory the child holds must stay flat.  A recycle cache that drops
// frees once it is full makes each round carve fresh memory instead.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-arena-churn requires CONF_fork\n");
    return 0;
}

#else

#include <osv/fork_arena.hh>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <unistd.h>

static const int rounds = 200;
static const int nchunks = 5000;
static const long tolerance = 8L << 20;

enum { CHILD_OK = 55, CHILD_GREW = 66, CHILD_NOMEM = 77, CHILD_OUTSIDE = 88 };

static long free_bytes()
{
    struct sysinfo si;
    sysinfo(&si);
    return (long)si.freeram * si.mem_unit;
}

static size_t size_of(int i)
{
    return i % 50 == 0 ? 64 * 1024 : 1 + (i * 131) % 4000;
}

static void *ptrs[nchunks];

static int child()
{
    long base = 0;
#ifdef FORK_ARENA_HAS_STATS
    unsigned long cbase = 0;
#endif
    for (int r = 0; r < rounds; r++) {
        for (int i = 0; i < nchunks; i++) {
            ptrs[i] = malloc(size_of(i));
            if (!ptrs[i]) {
                return CHILD_NOMEM;
            }
            if (!fork_arena::contains(ptrs[i])) {
                return CHILD_OUTSIDE;
            }
            memset(ptrs[i], r, size_of(i));
        }
        for (int i = 0; i < nchunks; i++) {
            free(ptrs[i]);
        }
        if (r == 1) {
            base = free_bytes();
#ifdef FORK_ARENA_HAS_STATS
            cbase = fork_arena::stats().committed_bytes;
#endif
        } else if (r > 1) {
            long grew = base - free_bytes();
#ifdef FORK_ARENA_HAS_STATS
            long cgrew = long(fork_arena::stats().committed_bytes) - long(cbase);
            if (cgrew > 0) {
                printf("round %d: arena committed %ld KiB more than after round 2\n", r, cgrew >> 10);
                return CHILD_GREW;
            }
#endif
            if (grew > tolerance) {
                printf("round %d: %ld KiB more memory in use than after round 2\n", r, grew >> 10);
                return CHILD_GREW;
            }
        }
    }
    printf("child: %d rounds of %d chunks, memory flat after round 2 (%ld KiB drift)\n",
           rounds, nchunks, (base - free_bytes()) >> 10);
    return CHILD_OK;
}

int main()
{
    errno = 0;
    pid_t pid = fork();
    if (pid == -1 && errno == ENOSYS) {
        printf("SKIP: fork() not enabled\n");
        return 0;
    }
    if (pid == 0) {
        _exit(child());
    }
    int st = 0, failures = 0;
    waitpid(pid, &st, 0);
    int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    if (code != CHILD_OK) {
        printf("FAIL: child exit %d (%s)\n", code,
               code == CHILD_GREW ? "memory grew while the live heap stayed the same" :
               code == CHILD_NOMEM ? "malloc returned NULL" :
               code == CHILD_OUTSIDE ? "malloc returned memory outside the fork arena" :
               "crashed");
        failures++;
    } else {
        printf("PASS: tst-fork-arena-churn\n");
    }
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}

#endif
