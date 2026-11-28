/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A fork child that maps memory with 2 MiB pages of its own must give that
// memory back when it exits.  Private anonymous mappings of 2 MiB and more,
// and malloc() of 1 MiB and more, get 2 MiB pages; a server that forks a
// child per connection would lose all of it on every exit.
//
// Cases, each run by children forked one after another, each child mapping
// and touching its memory and exiting without freeing it:
//   mmap  - 16 private anonymous mappings of 2 MiB
//   malloc - 64 malloc() blocks of 1 MiB
//   shm   - the child maps a shared memory object the parent created, asks
//           for 2 MiB pages and writes it; its pages are the object's, so
//           the child's exit must free none of them, and the parent must
//           still read every byte the children wrote.
// Free memory is sampled after a warm-up child and after the last child is
// reaped (the reaper frees in the background, so it is polled for a while).

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const int CHILDREN = 6;
static const size_t MiB = 1 << 20;
// The leak is 32 MiB or more per child; allow page-pool and quarantine noise.
static const long LIMIT_KIB = 8 * 1024;
static const char *SHM_NAME = "/tst-fork-hugepage-exit";
static const size_t SHM_SIZE = 8 * MiB;
static const size_t SHM_MAP = 4 * MiB;  // less than the object: 2 MiB pages

static long free_kib()
{
    struct sysinfo si;
    sysinfo(&si);
    return (long)(si.freeram / 1024 * si.mem_unit);
}

static void touch(char *p, size_t n, char v)
{
    for (size_t i = 0; i < n; i += 4096) {
        p[i] = v;
    }
}

static int child_mmap()
{
    for (int i = 0; i < 16; i++) {
        char *p = static_cast<char *>(mmap(nullptr, 2 * MiB, PROT_READ | PROT_WRITE,
                                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (p == MAP_FAILED) {
            return 2;
        }
        touch(p, 2 * MiB, 1);
    }
    return 0;
}

static int child_malloc()
{
    for (int i = 0; i < 64; i++) {
        char *p = static_cast<char *>(malloc(MiB));
        if (!p) {
            return 2;
        }
        touch(p, MiB, 1);
    }
    return 0;
}

static int child_shm(int k)
{
    int fd = shm_open(SHM_NAME, O_RDWR, 0);
    if (fd < 0) {
        return 2;
    }
    char *p = static_cast<char *>(mmap(nullptr, SHM_MAP, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, fd, 0));
    if (p == MAP_FAILED) {
        return 3;
    }
    madvise(p, SHM_MAP, MADV_HUGEPAGE);
    // Each child writes its own number into its own 4 KiB slot of every
    // 64 KiB, after checking that the parent's pattern is still there.
    for (size_t off = 0; off < SHM_MAP; off += 65536) {
        if (p[off] != 'P') {
            return 4;
        }
        p[off + 4096 * (k + 1)] = (char)('a' + k);
    }
    return 0;
}

static bool run_children(const char *name, int n, int (*fn)(int))
{
    for (int k = 0; k < n; k++) {
        pid_t pid = fork();
        if (pid < 0) {
            printf("FAIL: %s: fork errno=%d\n", name, errno);
            return false;
        }
        if (pid == 0) {
            _exit(fn(k));
        }
        int st = 0;
        if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st) || WEXITSTATUS(st)) {
            printf("FAIL: %s: child %d exit status 0x%x\n", name, k, st);
            return false;
        }
    }
    return true;
}

// Free memory lost by @n children of @fn, after one warm-up child.
static bool measure(const char *name, int (*fn)(int), long *lost)
{
    if (!run_children(name, 1, fn)) {
        return false;
    }
    sleep(1);
    long before = free_kib();
    if (!run_children(name, CHILDREN, fn)) {
        return false;
    }
    // The reaper frees a child's memory after waitpid() returns: wait until it
    // is back, or 10 s.
    long after = free_kib();
    for (int i = 0; i < 100 && before - after > LIMIT_KIB; i++) {
        usleep(100000);
        after = free_kib();
    }
    *lost = before - after;
    printf("%s: %d children, free memory %ld -> %ld KiB, %ld KiB not returned (%ld per child)\n",
           name, CHILDREN, before, after, *lost, *lost / CHILDREN);
    return true;
}

static int f_mmap(int) { return child_mmap(); }
static int f_malloc(int) { return child_malloc(); }
static int f_shm(int k) { return child_shm(k % 15); }

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
    setvbuf(stdout, nullptr, _IONBF, 0);

    int failures = 0;
    long lost;
    if (!measure("mmap", f_mmap, &lost)) {
        return 1;
    }
    if (lost > LIMIT_KIB) {
        printf("FAIL: mmap: a fork child's 2 MiB pages are not freed at exit\n");
        failures++;
    }
    if (!measure("malloc", f_malloc, &lost)) {
        return 1;
    }
    if (lost > LIMIT_KIB) {
        printf("FAIL: malloc: a fork child's 2 MiB pages are not freed at exit\n");
        failures++;
    }

    shm_unlink(SHM_NAME);
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd < 0 || ftruncate(fd, SHM_SIZE) < 0) {
        printf("FAIL: shm_open/ftruncate errno=%d\n", errno);
        return 1;
    }
    char *p = static_cast<char *>(mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, fd, 0));
    if (p == MAP_FAILED) {
        printf("FAIL: mmap shm errno=%d\n", errno);
        return 1;
    }
    for (size_t off = 0; off < SHM_SIZE; off += 4096) {
        p[off] = 'P';
    }
    if (!measure("shm", f_shm, &lost)) {
        return 1;
    }
    // Freeing the object's pages would return them: more than came in.
    if (lost < -LIMIT_KIB) {
        printf("FAIL: shm: a child's exit freed pages of a shared memory object\n");
        failures++;
    }
    // Reuse whatever memory came back, then check every child's writes.
    if (!run_children("reuse", 2, f_mmap)) {
        return 1;
    }
    int bad = 0;
    for (size_t off = 0; off < SHM_MAP; off += 65536) {
        if (p[off] != 'P') {
            bad++;
        }
        for (int k = 0; k < CHILDREN && k < 15; k++) {
            if (p[off + 4096 * (k + 1)] != (char)('a' + k)) {
                bad++;
            }
        }
    }
    if (bad) {
        printf("FAIL: shm: %d bytes the children wrote are gone\n", bad);
        failures++;
    }
    munmap(p, SHM_SIZE);
    close(fd);
    shm_unlink(SHM_NAME);

    if (!failures) {
        printf("PASS: fork children's 2 MiB pages are freed at exit, shared ones are kept\n");
    }
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
