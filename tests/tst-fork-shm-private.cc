/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Pages that belong to a file (a POSIX shm object) rather than to the page
// cache or to anonymous memory must not be freed by fork's copy-on-write
// paths. Map a shm object MAP_PRIVATE, fork, write the mapping in the parent
// and in the child, let the child exit, then close and unlink the object. A
// frame freed by the fault path or by child teardown would then be freed a
// second time when the object is closed, and the next fork or allocation
// would corrupt memory. Also mixes in a child that writes the mapping, and a
// child that forks a grandchild, and repeats so a double free shows up. Each
// child checks that it sees its own writes, the parent that it sees its own,
// and at the end enough pages are allocated and freed to push any page freed
// twice out of the allocator's per-CPU quarantine and into two owners.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static const size_t SIZE = 64 * 4096;
static const int ROUNDS = 200;

static int failures;

static bool round_trip(int r)
{
    char name[64];
    snprintf(name, sizeof name, "/tst-fork-shm-private-%d", r);
    int fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd < 0 || ftruncate(fd, SIZE) != 0) {
        printf("FAIL: shm_open/ftruncate round %d (errno=%d)\n", r, errno);
        return false;
    }
    volatile char *p = (volatile char *)mmap(nullptr, SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
        printf("FAIL: mmap round %d (errno=%d)\n", r, errno);
        return false;
    }
    for (size_t o = 0; o < SIZE; o += 4096) {
        p[o] = 'a';
    }
    pid_t pid = fork();
    if (pid < 0) {
        printf("FAIL: fork round %d (errno=%d)\n", r, errno);
        return false;
    }
    if (pid == 0) {
        for (size_t o = 0; o < SIZE; o += 4096) {
            p[o] = 'c';
        }
        for (size_t o = 0; o < SIZE; o += 4096) {
            if (p[o] != 'c') {
                _exit(4);
            }
        }
        if (r % 4 == 0) {
            pid_t g = fork();
            if (g == 0) {
                for (size_t o = 0; o < SIZE; o += 8192) {
                    p[o] = 'g';
                }
                _exit(0);
            }
            waitpid(g, nullptr, 0);
        }
        _exit(0);
    }
    for (size_t o = 0; o < SIZE; o += 4096) {
        p[o] = 'p';
    }
    int st = 0;
    if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("FAIL: child round %d status 0x%x\n", r, st);
        return false;
    }
    for (size_t o = 0; o < SIZE; o += 4096) {
        if (p[o] != 'p') {
            printf("FAIL: round %d: parent's page at %zu reads '%c', not 'p'\n", r, o, p[o]);
            return false;
        }
    }
    munmap((void *)p, SIZE);
    close(fd);
    shm_unlink(name);
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

    for (int r = 0; r < ROUNDS; r++) {
        if (!round_trip(r)) {
            failures++;
            break;
        }
    }
    // Allocate and touch memory after the rounds: a page freed twice is
    // handed out twice, and two owners writing it shows up here.
    const size_t N = 8192;
    char *a[N];
    for (size_t i = 0; i < N; i++) {
        a[i] = (char *)malloc(4096);
        memset(a[i], (int)i, 4096);
    }
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < 4096; j += 512) {
            if (a[i][j] != (char)i) {
                printf("FAIL: allocation %zu was overwritten (a page is owned twice)\n", i);
                failures++;
                i = N;
                break;
            }
        }
    }
    for (size_t i = 0; i < N; i++) {
        free(a[i]);
    }
    if (!failures) {
        printf("PASS: %d rounds of fork with a MAP_PRIVATE shm mapping\n", ROUNDS);
    }
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
