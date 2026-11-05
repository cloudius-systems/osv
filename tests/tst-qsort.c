/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Regression test for CVE-2026-40200 in musl's qsort (smoothsort).
//
// Through musl 1.2.6 the doubleword "count trailing zeros" helper pntz()
// returned the wrong value when the first set bit was in the high word,
// letting trinkle() run past its stack array. Reaching that state through
// qsort() needs more elements than the 64th Leonardo number, so on a
// 64-bit target the only practical check is white-box: compile the
// qsort.c the kernel is built from and test pntz() directly. Fixed by
// musl 228da39e (with b3291b9a and 5122f9f3 hardening the same code).
//
// The rest checks the kernel's qsort() and qsort_r(), which must both be
// musl's implementation now that OSv's private qsort_r copy is gone.

#define weak_alias(a, b)
#define __qsort_r musl_src_qsort_r
#include "../musl/src/stdlib/qsort.c"
#undef __qsort_r

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// OSv's <stdlib.h> does not declare it; the kernel exports musl's.
void qsort_r(void *, size_t, size_t,
             int (*)(const void *, const void *, void *), void *);

static int tests, fails;

static void report(int ok, const char *msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", msg);
}

static void check_pntz(void)
{
    const int bits = 8 * sizeof(size_t);
    int bad = 0;
    for (int k = 0; k < bits; k++) {
        size_t p[2] = { 1, (size_t)1 << k };
        int r = pntz(p);
        if (r != bits + k) {
            printf("  pntz({1, 1<<%d}) = %d, want %d\n", k, r, bits + k);
            bad++;
        }
    }
    for (int j = 1; j < bits; j++) {
        size_t p[2] = { 1 | (size_t)1 << j, 1 };
        int r = pntz(p);
        if (r != j) {
            printf("  pntz({1|1<<%d, 1}) = %d, want %d\n", j, r, j);
            bad++;
        }
    }
    report(bad == 0, "pntz() of musl's qsort.c across both words");
}

struct rec {
    uint32_t key;
    uint32_t id;
    unsigned char pad[292];  // 300 bytes: exercises cycle()'s 256-byte chunks
};

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static int cmp_rec_r(const void *a, const void *b, void *arg)
{
    ++*(size_t *)arg;
    return cmp_u32(a, b);
}

static uint32_t rnd_state = 12345;
static uint32_t rnd(void)
{
    rnd_state = rnd_state * 1103515245 + 12345;
    return rnd_state >> 8;
}

static void check_qsort(size_t n)
{
    uint32_t *a = malloc(n * sizeof(*a) + 1);
    uint64_t sum = 0, sum2 = 0;
    for (size_t i = 0; i < n; i++) {
        a[i] = rnd() % (n + 1);
        sum += a[i];
    }
    qsort(a, n, sizeof(*a), cmp_u32);
    int ok = 1;
    for (size_t i = 0; i < n; i++) {
        sum2 += a[i];
        if (i && a[i - 1] > a[i]) {
            ok = 0;
        }
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "qsort n=%zu", n);
    report(ok && sum == sum2, msg);
    free(a);
}

static void check_qsort_r(size_t n)
{
    struct rec *a = malloc(n * sizeof(*a) + 1);
    for (size_t i = 0; i < n; i++) {
        a[i].key = rnd() % 64;
        a[i].id = i;
        memset(a[i].pad, (unsigned char)i, sizeof(a[i].pad));
    }
    size_t calls = 0;
    qsort_r(a, n, sizeof(*a), cmp_rec_r, &calls);
    int ok = n < 2 || calls > 0;
    unsigned char *seen = calloc(n + 1, 1);
    for (size_t i = 0; i < n; i++) {
        if (i && a[i - 1].key > a[i].key) {
            ok = 0;
        }
        // Each record must arrive whole and exactly once.
        if (a[i].id >= n || seen[a[i].id]++ ||
            a[i].pad[0] != (unsigned char)a[i].id ||
            a[i].pad[sizeof(a[i].pad) - 1] != (unsigned char)a[i].id) {
            ok = 0;
        }
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "qsort_r n=%zu width=%zu", n, sizeof(*a));
    report(ok, msg);
    free(seen);
    free(a);
}

int main(void)
{
    check_pntz();

    // 0..300 covers every small Leonardo-heap shape; then sizes on both
    // sides of a few Leonardo numbers, and one large array.
    for (size_t n = 0; n <= 300; n++) {
        check_qsort(n);
    }
    static const size_t big[] = { 1972, 1973, 1974, 3193, 3194, 3195, 1 << 20 };
    for (size_t i = 0; i < sizeof(big) / sizeof(big[0]); i++) {
        check_qsort(big[i]);
    }
    static const size_t recs[] = { 0, 1, 2, 3, 41, 1000, 20000 };
    for (size_t i = 0; i < sizeof(recs) / sizeof(recs[0]); i++) {
        check_qsort_r(recs[i]);
    }

    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails != 0;
}
