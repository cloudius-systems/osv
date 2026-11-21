/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A fork child may hold more than 2 GiB of heap.  The child allocates
// 2.25 GiB in 256 KiB arena-class chunks, writes each, and checks that every
// one is fork-arena memory; after it exits, its memory must be returned.
// A fixed per-process arena window sends the excess to the shared kernel
// heap, where nothing frees it.  Needs a guest with about 4 GiB or more free.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-arena-big requires CONF_fork\n");
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

static const size_t chunk = 256 * 1024 - 16;   // one 256 KiB class with its header
static const int nchunks = 9216;               // 2.25 GiB
static const long tolerance = 32L << 20;

enum { CHILD_OK = 55, CHILD_NOMEM = 77, CHILD_OUTSIDE = 88 };

static long free_bytes()
{
    struct sysinfo si;
    sysinfo(&si);
    return (long)si.freeram * si.mem_unit;
}

int main()
{
    long need = long(nchunks) * 256 * 1024 + (512L << 20);
    if (free_bytes() < need) {
        printf("SKIP: tst-fork-arena-big needs %ld MiB free, have %ld MiB\n",
               need >> 20, free_bytes() >> 20);
        return 0;
    }
    errno = 0;
    pid_t probe = fork();
    if (probe == -1 && errno == ENOSYS) {
        printf("SKIP: fork() not enabled\n");
        return 0;
    }
    if (probe == 0) {
        _exit(0);
    }
    waitpid(probe, nullptr, 0);
    sleep(1);
    long before = free_bytes();

    pid_t pid = fork();
    if (pid == 0) {
        for (int i = 0; i < nchunks; i++) {
            char *p = static_cast<char*>(malloc(chunk));
            if (!p) {
                printf("child: malloc %d returned NULL\n", i);
                _exit(CHILD_NOMEM);
            }
            if (!fork_arena::contains(p)) {
                printf("child: chunk %d (%zu MiB in) at %p is outside the fork arena\n",
                       i, size_t(i) * chunk >> 20, p);
                _exit(CHILD_OUTSIDE);
            }
            memset(p, i, chunk);
        }
        printf("child: %d chunks, %zu MiB, all in the fork arena\n", nchunks,
               size_t(nchunks) * chunk >> 20);
        _exit(CHILD_OK);
    }
    int st = 0, failures = 0;
    waitpid(pid, &st, 0);
    int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    if (code != CHILD_OK) {
        printf("FAIL: child exit %d (%s)\n", code,
               code == CHILD_NOMEM ? "malloc returned NULL" :
               code == CHILD_OUTSIDE ? "heap past 2 GiB left the fork arena" : "crashed");
        failures++;
    }
    long lost = 0;
    for (int i = 0; i < 300; i++) {
        lost = before - free_bytes();
        if (lost <= tolerance) {
            break;
        }
        usleep(100000);
    }
    printf("free memory: %ld KiB not returned after the child exited\n", lost >> 10);
    if (lost > tolerance) {
        printf("FAIL: the child's memory was not returned\n");
        failures++;
    }
    if (!failures) {
        printf("PASS: tst-fork-arena-big\n");
    }
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}

#endif
