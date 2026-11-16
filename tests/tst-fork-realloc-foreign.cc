/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// realloc() and malloc_usable_size() of fork arena pointers the caller does
// not own.  With no argument, the cases that must work:
//  - a child reallocs, and asks the size of, chunks it inherited from its
//    parent after the parent freed or reused them: the child's copy keeps its
//    contents and its size;
//  - a child reallocs its own chunks.
// With the argument "unmapped", the parent reallocs a chunk of a live child,
// which is not mapped in the parent.  No size can be known there, so the
// kernel must abort with a message naming the pointer, not fault on it or
// read another process's memory.  That mode never returns.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-realloc-foreign requires CONF_fork\n");
    return 0;
}

#else

#include <osv/fork_arena.hh>
#include <atomic>
#include <new>
#include <errno.h>
#include <malloc.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static const size_t sz = 300;
static const int n = 64;

static void fill(void *p, size_t len, unsigned char seed)
{
    auto c = static_cast<unsigned char*>(p);
    for (size_t i = 0; i < len; i++) {
        c[i] = seed + i * 7;
    }
}

static bool check(const void *p, size_t len, unsigned char seed)
{
    auto c = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < len; i++) {
        if (c[i] != (unsigned char)(seed + i * 7)) {
            return false;
        }
    }
    return true;
}

struct shared {
    std::atomic<uintptr_t> ptr;
    std::atomic<int> go;
};

static int inherited()
{
    void *x[n];
    for (int i = 0; i < n; i++) {
        x[i] = malloc(sz);
        fill(x[i], sz, i);
    }
    if (!fork_arena::contains(x[0])) {
        printf("FAIL: setup (the parent's chunks are not in the arena)\n");
        return 1;
    }
    int p[2];
    if (pipe(p)) {
        printf("FAIL: pipe\n");
        return 1;
    }
    pid_t c = fork();
    if (c == 0) {
        char b;
        close(p[1]);
        read(p[0], &b, 1);   // the parent has freed and reused them
        int bad = 0;
        for (int i = 0; i < n; i++) {
            if (malloc_usable_size(x[i]) < sz) {
                bad |= 1;
            }
            void *y = realloc(x[i], 2 * sz);
            if (!y || !check(y, sz, i)) {
                bad |= 2;
            }
            free(y);
        }
        void *own = malloc(sz);
        fill(own, sz, 99);
        void *own2 = realloc(own, 8 * sz);
        if (!own2 || !check(own2, sz, 99) || malloc_usable_size(own2) < 8 * sz) {
            bad |= 4;
        }
        free(own2);
        _exit(bad);
    }
    close(p[0]);
    for (int i = 0; i < n; i++) {
        free(x[i]);
    }
    for (int i = 0; i < n; i++) {
        x[i] = malloc(sz);
        memset(x[i], 0xee, sz);
    }
    write(p[1], "g", 1);
    close(p[1]);
    int st = 0;
    waitpid(c, &st, 0);
    int r = WIFEXITED(st) ? WEXITSTATUS(st) : 255;
    printf("inherited: child exit %d (1: size, 2: contents, 4: own realloc)\n", r);
    return r != 0;
}

static void unmapped()
{
    void *m = mmap(nullptr, sizeof(shared), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    auto sh = new (m) shared{};
    pid_t c = fork();
    if (c == 0) {
        void *q = malloc(sz);
        fill(q, sz, 5);
        sh->ptr = reinterpret_cast<uintptr_t>(q);
        while (!sh->go.load()) {
            sched_yield();
        }
        _exit(0);
    }
    while (!sh->ptr.load()) {
        sched_yield();
    }
    void *q = reinterpret_cast<void*>(sh->ptr.load());
    printf("unmapped: realloc of the child's %p from the parent; this must abort\n", q);
    fflush(stdout);
    void *r = realloc(q, 2 * sz);
    printf("FAIL: realloc of a chunk not mapped here returned %p\n", r);
    sh->go = 1;
    waitpid(c, nullptr, 0);
}

int main(int argc, char **argv)
{
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
    if (argc > 1 && !strcmp(argv[1], "unmapped")) {
        unmapped();
        return 1;
    }
    int failures = inherited();
    if (!failures) {
        printf("PASS: tst-fork-realloc-foreign\n");
    }
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}

#endif
