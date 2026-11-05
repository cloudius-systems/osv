/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Regression test for CVE-2026-6042 in musl's iconv GB18030 decoder.
// Through musl 1.2.6 a 4-byte sequence was decoded by rescanning the whole
// 2-byte table once per step, up to a quarter second per character on a
// fast machine, and the result was wrong above the surrogate range and
// outside the BMP. Fixed by musl 67219f01. Expected values match glibc.

#include <errno.h>
#include <iconv.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int tests, fails;

static void report(int ok, const char *msg, const unsigned char *s)
{
    ++tests;
    fails += !ok;
    printf("%s: %s %02x %02x %02x %02x\n", ok ? "PASS" : "FAIL", msg,
           s[0], s[1], s[2], s[3]);
}

// Decode one 4-byte sequence; returns the code point or -errno.
static long decode(iconv_t cd, const unsigned char *s)
{
    unsigned char out[4];
    char *in = (char *)s, *op = (char *)out;
    size_t inb = 4, outb = sizeof(out);
    if (iconv(cd, &in, &inb, &op, &outb) == (size_t)-1) {
        return -errno;
    }
    if (inb != 0 || outb != 0) {
        return -EINVAL;
    }
    return out[0] | out[1] << 8 | out[2] << 16 | (long)out[3] << 24;
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(void)
{
    iconv_t cd = iconv_open("UTF-32LE", "GB18030");
    if (cd == (iconv_t)-1) {
        printf("FAIL: iconv_open(UTF-32LE, GB18030): %s\n", strerror(errno));
        return 1;
    }

    static const struct {
        unsigned char s[4];
        long cp;
    } good[] = {
        { { 0x81, 0x30, 0x81, 0x30 }, 0x0080 },
        { { 0x82, 0x35, 0x8f, 0x33 }, 0x9fa6 },
        { { 0x83, 0x36, 0xc9, 0x30 }, 0xe7f0 },   // 1.2.6: 0xd80b, a surrogate
        { { 0x84, 0x30, 0x85, 0x34 }, 0xf92b },
        { { 0x84, 0x31, 0xa4, 0x37 }, 0xfffd },
        { { 0x84, 0x31, 0xa4, 0x39 }, 0xffff },
        { { 0x90, 0x30, 0x81, 0x30 }, 0x10000 },  // 1.2.6: 0x3404c
        { { 0xe3, 0x32, 0x9a, 0x35 }, 0x10ffff }, // 1.2.6: 0x13404b
    };
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        long cp = decode(cd, good[i].s);
        char msg[64];
        snprintf(msg, sizeof(msg), "U+%04lX (got %#lx)", good[i].cp, cp);
        report(cp == good[i].cp, msg, good[i].s);
    }

    // Past U+10FFFF, and the unassigned gap between the BMP and U+10000.
    static const unsigned char bad[][4] = {
        { 0xe3, 0x32, 0x9a, 0x36 },
        { 0x84, 0x31, 0xa5, 0x30 },
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        long r = decode(cd, bad[i]);
        report(r == -EILSEQ, "EILSEQ", bad[i]);
    }

    // The slowest index for the old decoder (U+9FA6). 32 of them took ~9 s
    // on 1.2.6; the fixed decoder needs microseconds.
    const unsigned char *slow = good[1].s;
    double t0 = now();
    for (int i = 0; i < 32; i++) {
        decode(cd, slow);
    }
    double dt = now() - t0;
    char msg[64];
    snprintf(msg, sizeof(msg), "32 decodes in %.3f s (bound 1 s)", dt);
    report(dt < 1.0, msg, slow);

    iconv_close(cd);
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails != 0;
}
