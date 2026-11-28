/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// The page cache records every PTE slot that maps a cached file page, so a
// drop of the page can clear them.  A forking server keeps many children alive
// at once, and fork copies the parent's page-cache PTEs into each child, so a
// page the parent mapped is recorded once per live child.  This test keeps
// CHILDREN children alive over a large file mapped MAP_PRIVATE and MAP_SHARED,
// with only some pages touched before the fork, and checks:
//
//  - every child reads the file's data through both mappings;
//  - while the children live, the page cache records one slot per mapping per
//    child on each touched page, and none of them is stale;
//  - a drop of a touched page (a MAP_SHARED write fault on it moves the page
//    to the write cache) clears every child's slot: each child then reads the
//    new contents through MAP_SHARED, and the recorded slots stay exact;
//  - once the children exit, the file's recorded slots are back to the
//    parent's own, and none is stale;
//  - over many fork and exit cycles, neither free memory nor the number of
//    recorded slots grows.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <unistd.h>
#include <osv/pagecache.hh>

static const size_t PG = 4096;
static const int NPAGES = 1024;
static const int STRIDE = 7;              // touch every 7th page
static const int CHILDREN = 12;
static const int CYCLES = 1000;
static const long LIMIT_BYTES_PER_CYCLE = 16 * 1024;
static const char *PATH = "/tmp/fork-pagecache-many";

static int failures;
static dev_t g_dev;
static ino_t g_ino;

static void report(bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static osv_pagecache_audit_result audit()
{
    osv_pagecache_audit_result r;
    osv_pagecache_audit(g_dev, g_ino, &r);
    return r;
}

static long free_bytes()
{
    struct sysinfo si;
    if (sysinfo(&si) != 0) {
        return -1;
    }
    return (long)si.freeram * si.mem_unit;
}

static char fill(int pg)
{
    return (char)('A' + pg % 26);
}

static int touched_pages()
{
    return (NPAGES + STRIDE - 1) / STRIDE;
}

// Read every touched page through both mappings; return the number of pages
// whose first byte is not @want(pg) in either one.
static int bad_pages(const char *priv, const char *shared, char (*want)(int))
{
    int bad = 0;
    for (int pg = 0; pg < NPAGES; pg += STRIDE) {
        if (priv[pg * PG] != want(pg) || shared[pg * PG] != want(pg)) {
            bad++;
        }
    }
    return bad;
}

static char dropped(int pg)
{
    return pg == 0 ? 'Z' : fill(pg);
}

// A child: check the data, tell the parent, wait for the drop, check again.
static void child(const char *priv, const char *shared, int ready, int go)
{
    char c;
    int bad = bad_pages(priv, shared, fill) ? 1 : 0;
    if (write(ready, "r", 1) != 1 || read(go, &c, 1) != 1) {
        _exit(100);
    }
    // The parent wrote 'Z' to page 0 through MAP_SHARED.  The child's
    // MAP_SHARED view must show it; its untouched-by-it MAP_PRIVATE view of a
    // page it never wrote reads the file, so it shows it too.
    if (shared[0] != 'Z' || priv[0] != 'Z') {
        bad |= 2;
    }
    _exit(bad);
}

static void make_file()
{
    int fd = open(PATH, O_CREAT | O_TRUNC | O_RDWR, 0644);
    char buf[PG];
    for (int pg = 0; pg < NPAGES; pg++) {
        memset(buf, fill(pg), PG);
        if (write(fd, buf, PG) != (ssize_t)PG) {
            perror("write");
            _exit(1);
        }
    }
    fsync(fd);
    struct stat st;
    fstat(fd, &st);
    g_dev = st.st_dev;
    g_ino = st.st_ino;
    close(fd);
}

static void many_children(char *priv, char *shared, unsigned long base_slots)
{
    int ready[2], go[2];
    if (pipe(ready) || pipe(go)) {
        report(false, "pipe");
        return;
    }
    pid_t pids[CHILDREN];
    for (int i = 0; i < CHILDREN; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            child(priv, shared, ready[1], go[0]);
        }
    }
    char c;
    for (int i = 0; i < CHILDREN; i++) {
        if (read(ready[0], &c, 1) != 1) {
            report(false, "child ready");
        }
    }
    auto live = audit();
    printf("  slots: parent alone %lu, with %d children %lu (stale %lu)\n",
           base_slots, CHILDREN, live.slots, live.stale_slots);
    report(live.slots == base_slots * (CHILDREN + 1) && live.stale_slots == 0,
           "each live child adds one slot per touched page and mapping");

    shared[0] = 'Z';
    msync(shared, PG, MS_SYNC);
    auto after_drop = audit();
    report(after_drop.stale_slots == 0, "a drop leaves no stale slot");
    for (int i = 0; i < CHILDREN; i++) {
        if (write(go[1], "g", 1) != 1) {
            report(false, "pipe write");
        }
    }
    int bad = 0;
    for (int i = 0; i < CHILDREN; i++) {
        int st = 0;
        if (waitpid(pids[i], &st, 0) != pids[i] || !WIFEXITED(st) ||
                WEXITSTATUS(st)) {
            bad++;
        }
    }
    report(bad == 0, "every child read the file, then saw the dropped page");
    close(ready[0]); close(ready[1]); close(go[0]); close(go[1]);
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    errno = 0;
    pid_t probe = fork();
    if (probe == -1 && errno == ENOSYS) {
        printf("SKIP: fork() not enabled\n");
        printf("SUMMARY: 0 failures\n");
        // OSv can hold this line back after setvbuf(); flush it.
        fflush(stdout);
        return 0;
    }
    if (probe == 0) {
        _exit(0);
    }
    waitpid(probe, nullptr, 0);

    make_file();
    int fd = open(PATH, O_RDWR);
    char *priv = static_cast<char *>(mmap(nullptr, NPAGES * PG, PROT_READ,
                                          MAP_PRIVATE, fd, 0));
    char *shared = static_cast<char *>(mmap(nullptr, NPAGES * PG,
                                            PROT_READ | PROT_WRITE, MAP_SHARED,
                                            fd, 0));
    if (fd < 0 || priv == MAP_FAILED || shared == MAP_FAILED) {
        printf("FAIL: setup (errno %d)\nSUMMARY: 1 failures\n", errno);
        return 1;
    }
    report(bad_pages(priv, shared, fill) == 0, "the parent reads the file");
    auto base = audit();
    printf("  parent: %lu pages cached, %lu slots for %d touched pages\n",
           base.pages, base.slots, touched_pages());
    report(base.slots == (unsigned long)(2 * touched_pages()) &&
           base.stale_slots == 0, "the parent's own slots are recorded");

    many_children(priv, shared, base.slots);
    usleep(200 * 1000);
    report(bad_pages(priv, shared, dropped) == 0,
           "the parent reads the file after the drop");
    auto reaped = audit();
    printf("  slots after the children exit: %lu (stale %lu)\n", reaped.slots,
           reaped.stale_slots);
    report(reaped.slots == base.slots && reaped.stale_slots == 0,
           "reaped children leave no slot behind");

    for (int i = 0; i < 50; i++) {
        pid_t p = fork();
        if (p == 0) {
            _exit(0);
        }
        waitpid(p, nullptr, 0);
    }
    usleep(200 * 1000);
    long before = free_bytes();
    for (int i = 0; i < CYCLES; i++) {
        pid_t p = fork();
        if (p == 0) {
            _exit(priv[STRIDE * PG] == fill(STRIDE) ? 0 : 1);
        }
        waitpid(p, nullptr, 0);
    }
    usleep(200 * 1000);
    long after = free_bytes();
    auto end = audit();
    long per_cycle = (before - after) / CYCLES;
    printf("  free memory %ld -> %ld KiB over %d forks: %ld bytes per fork "
           "(limit %ld); slots %lu\n", before / 1024, after / 1024, CYCLES,
           per_cycle, LIMIT_BYTES_PER_CYCLE, end.slots);
    report(per_cycle <= LIMIT_BYTES_PER_CYCLE, "no memory leak across forks");
    report(end.slots == base.slots && end.stale_slots == 0,
           "no slot leak across forks");

    munmap(priv, NPAGES * PG);
    munmap(shared, NPAGES * PG);
    close(fd);
    unlink(PATH);
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
