/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// fork() in a fork child.  main forks P, P forks G; G checks it sees P's
// memory, writes its own heap, stack and globals, and exits; P reaps G and
// checks its own memory is unchanged; main reaps P and checks the same.
// Each process installs its own SIGUSR2 handler, and its child must inherit
// that one.  50 rounds, then one chain four processes deep.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-nested requires CONF_fork\n");
    return 0;
}

#else

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const size_t heap_sz = 64 * 1024;
static volatile unsigned long global_word;

static void h0(int) {}
static void h1(int) {}
static void h2(int) {}
static void h3(int) {}
static void h4(int) {}
static void (*const handlers[])(int) = { h0, h1, h2, h3, h4 };

static bool handler_is(int depth)
{
    struct sigaction sa;
    return sigaction(SIGUSR2, nullptr, &sa) == 0 && sa.sa_handler == handlers[depth];
}

static void set_handler(int depth)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handlers[depth];
    sigaction(SIGUSR2, &sa, nullptr);
}

static void fill(unsigned char *p, size_t n, unsigned char seed)
{
    for (size_t i = 0; i < n; i++) {
        p[i] = seed + i * 13;
    }
}

static bool check(const unsigned char *p, size_t n, unsigned char seed)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i] != (unsigned char)(seed + i * 13)) {
            return false;
        }
    }
    return true;
}

// Run as level @depth of a chain of @levels processes; return this level's
// exit code: 0 if everything it and its descendants checked held.
static int level(int depth, int levels, unsigned char *heap, unsigned char *parent_heap)
{
    unsigned char stack_buf[512];
    unsigned char seed = 0x10 * depth + 1;
    if (parent_heap && !check(parent_heap, heap_sz, seed - 0x10)) {
        return 10 + depth;   // the parent's memory did not reach this process
    }
    if (depth && !handler_is(depth - 1)) {
        return 50 + depth;   // the parent's signal handler was not inherited
    }
    set_handler(depth);
    fill(heap, heap_sz, seed);
    fill(stack_buf, sizeof(stack_buf), seed);
    global_word = 0x900d0000ul + depth;
    if (depth == levels) {
        return 0;
    }
    auto child_heap = static_cast<unsigned char*>(malloc(heap_sz));
    pid_t c = fork();
    if (c == 0) {
        _exit(level(depth + 1, levels, child_heap, heap));
    }
    if (c < 0) {
        return 20 + depth;   // fork failed
    }
    int st = 0;
    if (waitpid(c, &st, 0) != c || !WIFEXITED(st)) {
        return 30 + depth;   // not reaped, or did not exit normally
    }
    if (WEXITSTATUS(st)) {
        return WEXITSTATUS(st);
    }
    if (!check(heap, heap_sz, seed) || !check(stack_buf, sizeof(stack_buf), seed) ||
            global_word != 0x900d0000ul + depth) {
        return 40 + depth;   // a descendant's writes reached this process
    }
    free(child_heap);
    return 0;
}

int main()
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

    int failures = 0;
    auto heap = static_cast<unsigned char*>(malloc(heap_sz));
    for (int round = 0; round < 50; round++) {
        int r = level(0, 2, heap, nullptr);
        if (r) {
            printf("FAIL: round %d: code %d (1x: data, 2x: fork, 3x: reap, 4x: isolation, "
                   "5x: signal handler; x = depth)\n", round, r);
            failures++;
            break;
        }
    }
    int r = level(0, 4, heap, nullptr);
    if (r) {
        printf("FAIL: four-deep chain: code %d\n", r);
        failures++;
    }
    if (!failures) {
        printf("PASS: tst-fork-nested\n");
    }
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}

#endif
