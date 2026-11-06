/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Host timing of OSv's strcoll/strxfrm (collate.cc) against glibc's on the same data, for strings
// of 8, 64, 512 and 4096 bytes, then a long run of identical combining marks (U+0301) at
// 512..32768 characters (the position-level ignorable-run path that was once O(n^2); this
// case keeps it honest). A sanity check that per-call cost stays linear, not a benchmark claim.
//   g++ -std=gnu++17 -O2 -DOSV_COLLATE_HOST -Ilibc/locale scripts/locale-oracle/collate/coll-bench.cc \
//       libc/locale/collate.cc libc/locale/locale-data.cc -o coll-bench
//   LOCPATH=<dir> ./coll-bench <locale>

#include "locale-data.h"

#include <chrono>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 2 || !setlocale(LC_ALL, argv[1])) {
        return 1;
    }
    const osv_locale_data *d = osv_locale_load(LC_COLLATE, argv[1]);
    if (!d || !d->collate.nrules) {
        return 1;
    }
    const char *pieces[] = { "a", "B", "\xc3\xa9", "-", " ", "1", "\xe4\xb8\x80", "\xd0\xb0", "ch" };
    volatile long sink = 0;
    for (size_t len : { 8, 64, 512, 4096 }) {
        std::string a, b;
        for (size_t i = 0; a.size() < len; i++) {
            a += pieces[(i * 7) % 9];
        }
        b = a;
        b[b.size() - 1] = 'z';   // differ at the end: the whole first level is compared
        long iters = 2000000 / len + 10;
        auto t0 = std::chrono::steady_clock::now();
        for (long i = 0; i < iters; i++) {
            sink += strcoll(a.c_str(), b.c_str());
        }
        auto t1 = std::chrono::steady_clock::now();
        for (long i = 0; i < iters; i++) {
            sink += osv_strcoll(&d->collate, a.c_str(), b.c_str());
        }
        auto t2 = std::chrono::steady_clock::now();
        std::vector<char> x(16 * len + 64);
        for (long i = 0; i < iters; i++) {
            sink += strxfrm(x.data(), a.c_str(), x.size());
        }
        auto t3 = std::chrono::steady_clock::now();
        for (long i = 0; i < iters; i++) {
            sink += osv_strxfrm(&d->collate, x.data(), a.c_str(), x.size());
        }
        auto t4 = std::chrono::steady_clock::now();
        auto ns = [&](auto s, auto e) {
            return std::chrono::duration<double, std::nano>(e - s).count() / iters / a.size();
        };
        printf("%s len %zu: strcoll ns/byte glibc %.2f osv %.2f; strxfrm glibc %.2f osv %.2f\n", argv[1],
               a.size(), ns(t0, t1), ns(t1, t2), ns(t2, t3), ns(t3, t4));
    }
    // Long combining-mark run: U+0301 (combining acute) repeated. At a position
    // level this is one ignorable run; it must stay linear in N (each doubling
    // of N should ~double the time, not quadruple it). glibc is the reference.
    printf("-- long combining-mark run (U+0301), per-call us; linear if each row ~2x the previous --\n");
    double prev_x = 0, prev_c = 0;
    for (size_t N : { (size_t)512, (size_t)1024, (size_t)2048, (size_t)4096,
                      (size_t)8192, (size_t)16384, (size_t)32768 }) {
        std::string a;
        for (size_t i = 0; i < N; i++) {
            a += "\xcc\x81";
        }
        std::string bb = a;
        bb += "z";
        std::vector<char> xb(16 * a.size() + 64);
        long iters = 2000000 / (long)N + 1;
        auto u0 = std::chrono::steady_clock::now();
        for (long i = 0; i < iters; i++) {
            sink += osv_strxfrm(&d->collate, xb.data(), a.c_str(), xb.size());
        }
        auto u1 = std::chrono::steady_clock::now();
        for (long i = 0; i < iters; i++) {
            sink += strxfrm(xb.data(), a.c_str(), xb.size());
        }
        auto u2 = std::chrono::steady_clock::now();
        for (long i = 0; i < iters; i++) {
            sink += osv_strcoll(&d->collate, a.c_str(), bb.c_str());
        }
        auto u3 = std::chrono::steady_clock::now();
        for (long i = 0; i < iters; i++) {
            sink += strcoll(a.c_str(), bb.c_str());
        }
        auto u4 = std::chrono::steady_clock::now();
        auto per = [&](auto s2, auto e2) {
            return std::chrono::duration<double, std::micro>(e2 - s2).count() / iters;
        };
        double ox = per(u0, u1), oc = per(u2, u3);
        printf("  N=%zu strxfrm osv %.2f glibc %.2f (osv x%.2f); strcoll osv %.2f glibc %.2f (osv x%.2f)%s\n",
               N, ox, per(u1, u2), prev_x ? ox / prev_x : 0.0,
               oc, per(u3, u4), prev_c ? oc / prev_c : 0.0,
               (prev_x && ox / prev_x > 3.0) ? "  WARNING: superlinear strxfrm" : "");
        prev_x = ox;
        prev_c = oc;
    }
    return 0;
}
