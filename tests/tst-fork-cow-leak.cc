/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A forking server (PostgreSQL's postmaster) forks a child per connection and
// keeps writing its own memory while children come and go. Each fork makes
// the parent's private pages copy-on-write; the parent's next write to one of
// them copies it. Once the child that shared the original is reaped, nothing
// else maps it, and it must be freed. If it is not, free memory falls by about
// one page per page the parent writes per fork, until the guest runs out.
//
// The parent forks a child that exits at once, reaps it, then writes one byte
// to each of PAGES pages, and repeats. Free memory is sampled after a warm-up
// (freed pages first fill a bounded per-CPU quarantine) and again at the end.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <unistd.h>

static const int PAGES = 32;
static const long WARMUP = 500;
static const long CYCLES = 2000;
// Leaking every copied page is PAGES * 4 KiB = 128 KiB per cycle.
static const long LIMIT_BYTES_PER_CYCLE = 16 * 1024;

static volatile char buf[PAGES * 4096] __attribute__((aligned(4096)));

static long free_bytes()
{
    struct sysinfo si;
    if (sysinfo(&si) != 0) {
        return -1;
    }
    return (long)si.freeram * si.mem_unit;
}

static bool cycle(long i)
{
    pid_t pid = fork();
    if (pid < 0) {
        printf("FAIL: fork %ld (errno=%d)\n", i, errno);
        return false;
    }
    if (pid == 0) {
        _exit(0);
    }
    int st;
    if (waitpid(pid, &st, 0) != pid) {
        printf("FAIL: waitpid %ld (errno=%d)\n", i, errno);
        return false;
    }
    for (int p = 0; p < PAGES; p++) {
        buf[p * 4096] = (char)i;
    }
    return true;
}

int main()
{
    errno = 0;
    pid_t probe = fork();
    if (probe == -1 && errno == ENOSYS) {
        printf("SKIP: fork() not enabled\n");
        printf("SUMMARY: 0 failures\n");
        return 0;
    }
    if (probe == 0) {
        _exit(0);
    }
    waitpid(probe, nullptr, 0);

    for (long i = 0; i < WARMUP; i++) {
        if (!cycle(i)) {
            return 1;
        }
    }
    sleep(1);
    long before = free_bytes();
    for (long i = 0; i < CYCLES; i++) {
        if (!cycle(i)) {
            return 1;
        }
    }
    sleep(1);
    long after = free_bytes();
    long per_cycle = (before - after) / CYCLES;
    printf("free memory %ld -> %ld KiB over %ld fork cycles, %d pages written per cycle: "
           "%ld bytes per cycle (limit %ld)\n",
           before / 1024, after / 1024, CYCLES, PAGES, per_cycle, LIMIT_BYTES_PER_CYCLE);
    if (per_cycle > LIMIT_BYTES_PER_CYCLE) {
        printf("FAIL: copy-on-write pages leak across fork\n");
        printf("SUMMARY: 1 failures\n");
        return 1;
    }
    printf("PASS: free memory stays flat across fork cycles\n");
    printf("SUMMARY: 0 failures\n");
    return 0;
}
