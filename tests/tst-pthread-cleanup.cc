/*
 * Copyright (C) 2026 Greg Burd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Verify pthread_cleanup_push/pop: pop(1) runs the handler, pop(0) does not,
 * and nested handlers run in LIFO order. Before the fix the _pthread_cleanup_*
 * bodies were no-ops, so pop(1) silently skipped the handler.
 */

#include <pthread.h>
#include <stdio.h>

static int tests = 0, fails = 0;
#define expect(actual, expected) do { \
    ++tests; \
    if ((actual) != (expected)) { \
        ++fails; \
        printf("FAIL: %s:%d: %s: expected %d, saw %d\n", \
               __FILE__, __LINE__, #actual, (int)(expected), (int)(actual)); \
    } \
} while (0)

static int ran = 0;
static int order[4];
static int order_n = 0;

static void mark(void *x)
{
    ran += (long)x;
    order[order_n++] = (long)x;
}

static void *run_pop1(void *)
{
    pthread_cleanup_push(mark, (void *)1);
    pthread_cleanup_pop(1);   // must run mark(1)
    return nullptr;
}

static void *run_pop0(void *)
{
    pthread_cleanup_push(mark, (void *)10);
    pthread_cleanup_pop(0);   // must NOT run
    return nullptr;
}

static void *run_nested(void *)
{
    pthread_cleanup_push(mark, (void *)1);
    pthread_cleanup_push(mark, (void *)2);
    pthread_cleanup_pop(1);   // inner first (LIFO): mark(2)
    pthread_cleanup_pop(1);   // then mark(1)
    return nullptr;
}

int main()
{
    pthread_t t;

    ran = 0;
    pthread_create(&t, nullptr, run_pop1, nullptr);
    pthread_join(t, nullptr);
    expect(ran, 1);

    ran = 0;
    pthread_create(&t, nullptr, run_pop0, nullptr);
    pthread_join(t, nullptr);
    expect(ran, 0);

    ran = 0; order_n = 0;
    pthread_create(&t, nullptr, run_nested, nullptr);
    pthread_join(t, nullptr);
    expect(ran, 3);
    expect(order_n, 2);
    expect(order[0], 2);   // inner handler ran first
    expect(order[1], 1);

    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails == 0 ? 0 : 1;
}
