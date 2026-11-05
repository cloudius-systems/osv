/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Regression test: the page cache must never hold a pointer to a PTE that no
 * longer maps the cached page.
 *
 * pagecache::get() records the address of every PTE it points at a cached
 * file page (cached_page::_ptes), so that dropping the page can clear those
 * PTEs.  A drop clears every recorded slot without checking what it maps now.
 * Under fork, three paths changed a slot behind the cache's back:
 *  - the copy-on-write resolver gave a write-faulted private file page a
 *    fresh copy in place but left the slot recorded, so the next drop of the
 *    cached page cleared the process's private copy (its writes were lost and
 *    the next read came from the file);
 *  - clone_address_space() copied recorded PTEs into the child unrecorded, so
 *    a drop left the child mapping a page the cache had let go of;
 *  - destroy_address_space() freed a child's page-table pages while slots
 *    that the child itself had faulted in were still recorded, so a later drop
 *    wrote into a freed (possibly reused) page; it also freed a child-faulted
 *    MAP_SHARED write-cache page that the cache still owned.
 * The same resolver also privatized MAP_SHARED file pages, so a store through
 * MAP_SHARED after a read never reached the file.
 *
 * A drop is forced here with a MAP_SHARED write fault on the same file page,
 * which moves the page from the read cache to the write cache.  The cache's
 * own audit (osv_pagecache_audit) counts recorded slots that no longer map
 * their page, and drops that met one.  Both must stay 0.
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <osv/pagecache.hh>

static int failures;
static dev_t g_dev;
static ino_t g_ino;
static const size_t PG = 4096;
static const int NPAGES = 8;

static void report(bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static osv_pagecache_audit_result audit_file()
{
    osv_pagecache_audit_result r;
    osv_pagecache_audit(g_dev, g_ino, &r);
    return r;
}

static unsigned long g_flush_base;

static bool audit_clean(const char *when)
{
    auto r = audit_file();
    osv_pagecache_audit_result all;
    osv_pagecache_audit(0, 0, &all);
    printf("  audit %s: file pages=%lu slots=%lu stale=%lu | all stale=%lu "
           "stale_flushes=+%lu\n", when, r.pages, r.slots, r.stale_slots,
           all.stale_slots, all.stale_flushes - g_flush_base);
    return r.stale_slots == 0 && all.stale_slots == 0 &&
           all.stale_flushes == g_flush_base;
}

static char g_path[64];

static void make_file(const char *path, char fill)
{
    snprintf(g_path, sizeof(g_path), "%s-%c", path, fill);
    path = g_path;
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        perror("open");
        exit(1);
    }
    char buf[PG];
    for (int i = 0; i < NPAGES; i++) {
        memset(buf, fill + i, PG);
        if (write(fd, buf, PG) != (ssize_t)PG) {
            perror("write");
            exit(1);
        }
    }
    fsync(fd);
    struct stat st;
    fstat(fd, &st);
    g_dev = st.st_dev;
    g_ino = st.st_ino;
    close(fd);
}

// Drop the read-cache copy of page @pg: a MAP_SHARED write fault moves it to
// the write cache, clearing every PTE the read cache recorded for it.
static void force_drop(int fd, int pg, char val)
{
    char *s = (char *)mmap(nullptr, PG, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                           pg * PG);
    if (s == MAP_FAILED) {
        perror("mmap shared");
        exit(1);
    }
    s[100] = val;
    munmap(s, PG);
}

static char pread_byte(int fd, off_t off)
{
    char c = 0;
    if (pread(fd, &c, 1, off) != 1) {
        perror("pread");
    }
    return c;
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== tst-fork-pagecache-ptes ===\n");
    const char *path = "/tmp/fork-pagecache-ptes";
    {
        osv_pagecache_audit_result all;
        osv_pagecache_audit(0, 0, &all);
        g_flush_base = all.stale_flushes;
        // Loaded objects' relocated .data/.got pages are copy-on-write copies
        // of cached file pages: their slots must not still be recorded.
        printf("  audit start: all pages=%lu slots=%lu stale=%lu\n",
               all.pages, all.slots, all.stale_slots);
        report(all.stale_slots == 0, "start: no stale slots from loading the program");
    }

    // Case A (no fork): a written page of a MAP_PRIVATE file mapping must keep
    // its contents when the cached page it was copied from is dropped.
    {
        make_file(path, 'a');
        int fd = open(g_path, O_RDWR);
        char *p = (char *)mmap(nullptr, NPAGES * PG, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE, fd, 0);
        volatile char sink = p[0];   // read fault: maps the read-cache page
        (void)sink;
        p[0] = 'W';                  // write fault: private copy
        force_drop(fd, 0, 'S');
        report(p[0] == 'W', "A: private copy survives a page-cache drop");
        report(audit_clean("A"), "A: no stale page-cache slots");
        munmap(p, NPAGES * PG);
        close(fd);
        unlink(g_path);
    }

    // Case B: the same, in a fork child and in the parent after the fork.
    {
        make_file(path, 'b');
        int fd = open(g_path, O_RDWR);
        char *p = (char *)mmap(nullptr, NPAGES * PG, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE, fd, 0);
        for (int i = 0; i < NPAGES; i++) {
            volatile char sink = p[i * PG];
            (void)sink;
        }
        pid_t c = fork();
        if (c == 0) {
            p[PG] = 'C';
            force_drop(fd, 1, 'X');
            _exit(p[PG] == 'C' ? 0 : 3);
        }
        int st = 0;
        waitpid(c, &st, 0);
        report(WIFEXITED(st) && WEXITSTATUS(st) == 0,
               "B: child's private copy survives a drop");
        p[2 * PG] = 'P';
        force_drop(fd, 2, 'Y');
        report(p[2 * PG] == 'P', "B: parent's private copy survives a drop after fork");
        report(audit_clean("B"), "B: no stale page-cache slots");
        munmap(p, NPAGES * PG);
        close(fd);
        unlink(g_path);
    }

    // Case C: inherited slots.  The child inherits read-cache mappings from
    // the parent; dropping the page must stop the child mapping it.
    {
        make_file(path, 'c');
        int fd = open(g_path, O_RDWR);
        char *p = (char *)mmap(nullptr, NPAGES * PG, PROT_READ, MAP_PRIVATE, fd, 0);
        for (int i = 0; i < NPAGES; i++) {
            volatile char sink = p[i * PG];
            (void)sink;
        }
        int ready[2], go[2];
        if (pipe(ready) || pipe(go)) {
            perror("pipe");
            return 1;
        }
        pid_t c = fork();
        if (c == 0) {
            char b;
            (void)!write(ready[1], "r", 1);
            (void)!read(go[0], &b, 1);
            // The parent dropped page 3 and rewrote it through MAP_SHARED.
            // A private mapping that was never written reads the file.
            _exit(p[3 * PG + 100] == 'Z' ? 0 : 4);
        }
        char b;
        (void)!read(ready[0], &b, 1);
        force_drop(fd, 3, 'Z');
        auto r = audit_file();
        printf("  C: file slots while child alive = %lu\n", r.slots);
        (void)!write(go[1], "g", 1);
        int st = 0;
        waitpid(c, &st, 0);
        report(WIFEXITED(st) && WEXITSTATUS(st) == 0,
               "C: child sees the file after the page it inherited was dropped");
        report(audit_clean("C"), "C: no stale page-cache slots");
        munmap(p, NPAGES * PG);
        close(fd);
        close(ready[0]); close(ready[1]); close(go[0]); close(go[1]);
    }

    // Case D: slots a child faulted in itself must be gone after it is
    // reaped, and dropping those pages afterwards must not write anywhere.
    {
        make_file(path, 'd');
        int fd = open(g_path, O_RDWR);
        char *p = (char *)mmap(nullptr, NPAGES * PG, PROT_READ, MAP_PRIVATE, fd, 0);
        auto before = audit_file();
        pid_t c = fork();
        if (c == 0) {
            int sum = 0;
            for (int i = 0; i < NPAGES; i++) {
                sum += p[i * PG];
            }
            _exit(sum ? 0 : 5);
        }
        int st = 0;
        waitpid(c, &st, 0);
        report(WIFEXITED(st) && WEXITSTATUS(st) == 0, "D: child read the file");
        // The reaper tears the child down after waitpid returns; give it time.
        usleep(200 * 1000);
        auto after = audit_file();
        printf("  D: file slots before fork %lu, after child reaped %lu\n",
               before.slots, after.slots);
        report(after.slots == before.slots, "D: reaped child's slots unregistered");
        report(audit_clean("D (before drops)"), "D: no stale slots after reap");
        for (int i = 0; i < NPAGES; i++) {
            force_drop(fd, i, 'D');
        }
        report(audit_clean("D (after drops)"), "D: drops met no stale slot");
        munmap(p, NPAGES * PG);
        close(fd);
        unlink(g_path);
    }

    // Case E: a MAP_SHARED write, in the parent and in a child, after the page
    // was read, must reach the file.
    {
        make_file(path, 'e');
        int fd = open(g_path, O_RDWR);
        char *s = (char *)mmap(nullptr, NPAGES * PG, PROT_READ | PROT_WRITE,
                               MAP_SHARED, fd, 0);
        volatile char sink = s[4 * PG];
        (void)sink;
        s[4 * PG + 7] = 'M';
        msync(s, NPAGES * PG, MS_SYNC);
        report(pread_byte(fd, 4 * PG + 7) == 'M',
               "E: MAP_SHARED write after a read reaches the file");
        sink = s[5 * PG];
        pid_t c = fork();
        if (c == 0) {
            s[5 * PG + 9] = 'K';
            msync(s, NPAGES * PG, MS_SYNC);
            _exit(0);
        }
        int st = 0;
        waitpid(c, &st, 0);
        report(s[5 * PG + 9] == 'K', "E: child's MAP_SHARED write visible to parent");
        report(pread_byte(fd, 5 * PG + 9) == 'K', "E: child's MAP_SHARED write reaches the file");
        // The child faulted page 6 in through MAP_SHARED; after it is reaped
        // the parent must still read the right page.
        c = fork();
        if (c == 0) {
            s[6 * PG + 11] = 'Q';
            _exit(0);
        }
        waitpid(c, &st, 0);
        usleep(200 * 1000);
        // Take fresh pages (anonymous memory comes from the same page pool),
        // so a page wrongly freed by the child's teardown gets reused.
        char *t = (char *)mmap(nullptr, 256 * PG, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
        memset(t, 0x5a, 256 * PG);
        report(s[6 * PG + 11] == 'Q' && s[6 * PG] == 'e' + 6,
               "E: page a reaped child wrote through MAP_SHARED is intact");
        munmap(t, 256 * PG);
        report(audit_clean("E"), "E: no stale page-cache slots");
        munmap(s, NPAGES * PG);
        close(fd);
        unlink(g_path);
    }

    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
