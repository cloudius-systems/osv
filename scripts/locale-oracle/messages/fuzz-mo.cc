/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Fuzz OSv's .mo reader and plural-expression parser (libc/locale/mo.cc) on
// Linux under ASan and UBSan. Build and run (from the OSv tree):
//
//   g++ -std=gnu++17 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all
//       -Ilibc/locale scripts/locale-oracle/messages/fuzz-mo.cc libc/locale/mo.cc -o fuzz-mo
//   ./fuzz-mo <iterations> <rng-seed> <file.mo>...
//   ./fuzz-mo --self-test <a| b>       (must die: the negative test of the harness)
//
// Each input is a seed catalog copied into a buffer of exactly its size
// (so a read past the end is an ASan report) and mutated: bit flips, header
// and table words set to boundary values (0, 1, size-1, size, 2^31, 2^32-1,
// offsets just past the end), truncation, a hash table pointed at random
// entries, NULs removed (unterminated strings). Every accepted file is then
// read through osv_mo_find for every original it lists, random strings, ""
// and the header's plural rule, and every returned translation is walked to
// its NUL inside the buffer. Plural expressions: random token strings (with
// deep nesting, huge literals, every operator, division by zero) through
// osv_plural_compile and osv_plural_index for n in a fixed set; the parser
// must not recurse (deep inputs would overflow the stack) and must keep its
// 9999-entry limit.
// Deterministic negatives first (truncated headers, offsets past the end,
// huge counts, a bad magic, major revision 2): each must be rejected or
// answer nothing, and every seed must be accepted unchanged.

#include "mo.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

std::mt19937_64 rng;
uint64_t rnd(uint64_t n) { return n ? rng() % n : 0; }

std::vector<char> read_file(const char *path)
{
    std::vector<char> v;
    FILE *f = fopen(path, "rb");
    if (!f) {
        return v;
    }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        v.insert(v.end(), buf, buf + n);
    }
    fclose(f);
    return v;
}

volatile unsigned long sink;

// Read every string the reader hands out, to its NUL.
void touch(const char *s, size_t len, const char *base, size_t size)
{
    if (s < base || s + len >= base + size || s[len]) {
        fprintf(stderr, "BUG: translation outside the buffer or not terminated\n");
        abort();
    }
    for (size_t i = 0; i <= len; i++) {
        sink += (unsigned char)s[i];
    }
}

uint32_t rd(const char *b, size_t off, bool sw)
{
    uint32_t w;
    memcpy(&w, b + off, 4);
    return sw ? __builtin_bswap32(w) : w;
}

// Everything one accepted catalog can be asked.
void exercise(const char *buf, size_t size)
{
    osv_mo m;
    if (!osv_mo_open(buf, size, &m)) {
        return;
    }
    size_t len;
    uint32_t idx;
    const char *h = osv_mo_find(&m, "", &len, &idx);
    if (h) {
        touch(h, len, buf, size);
        // the header as the plural parser sees it: a NUL-terminated copy
        std::string hs(h, len);
        osv_plural p;
        osv_plural_from_header(hs.c_str(), &p);
        for (unsigned long n : { 0UL, 1UL, 2UL, 5UL, 11UL, 21UL, 101UL, ~0UL }) {
            sink += osv_plural_index(&p, n);
        }
        osv_plural_free(&p);
    }
    // every original the tables name, if it is a string within the buffer
    uint32_t n = m.n < 4096 ? m.n : 4096;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t e = m.otab + 8ull * i;
        uint32_t l = rd(buf, e, m.swap), o = rd(buf, e + 4, m.swap);
        if (uint64_t(o) + l < size && !buf[uint64_t(o) + l]) {
            const char *t = osv_mo_find(&m, buf + o, &len, &idx);
            if (t) {
                touch(t, len, buf, size);
            }
        }
    }
    for (const char *q : { "alpha", "zeta", "x", "hello", "file", "k1", "\004" }) {
        const char *t = osv_mo_find(&m, q, &len, &idx);
        if (t) {
            touch(t, len, buf, size);
        }
    }
}

void mutate(std::vector<char> &v)
{
    size_t size = v.size();
    int k = 1 + rnd(6);
    for (int j = 0; j < k && size >= 4; j++) {
        switch (rnd(7)) {
        case 0: v[rnd(size)] ^= char(1 << rnd(8)); break;
        case 1: case 2: { // a header or table word set to a boundary value
            size_t off = rnd(4) ? 4 * rnd(7) : 4 * rnd(size / 4);
            if (off + 4 > size) break;
            uint32_t vals[] = { 0, 1, 2, 3, uint32_t(size - 1), uint32_t(size), uint32_t(size + 1),
                                uint32_t(size - 4), 0x7fffffff, 0x80000000u, 0xffffffffu,
                                0xfffffffcu, uint32_t(rng()), 0x10000, 0x20000 };
            uint32_t w = vals[rnd(sizeof(vals) / 4)];
            if (rnd(2)) w = __builtin_bswap32(w);
            memcpy(&v[off], &w, 4);
            break;
        }
        case 3: { // a table entry: offset just past the end, or huge length
            size_t off = 28 + 8 * rnd((size - 28) / 8 + 1);
            if (off + 8 > size) break;
            uint32_t l = rnd(2) ? uint32_t(rng()) : uint32_t(size - rnd(8)), o = uint32_t(size - rnd(8));
            memcpy(&v[off], &l, 4);
            memcpy(&v[off + 4], &o, 4);
            break;
        }
        case 4: v.resize(rnd(size + 1)); size = v.size(); break; // truncate
        case 5: // unterminated strings: NULs in the tail replaced
            for (size_t i = size - 1, c = rnd(64); c && i > 28; i--) {
                if (!v[i]) { v[i] = 'x'; c--; }
            }
            break;
        case 6: { // the hash table pointed at random entries
            if (size < 28) break;
            uint32_t hs = 3 + rnd(64), ho = uint32_t(28 + rnd(size));
            memcpy(&v[20], &hs, 4);
            memcpy(&v[24], &ho, 4);
            break;
        }
        }
    }
}

std::string random_expr()
{
    static const char *toks[] = { "n", "0", "1", "2", "3", "10", "100", "18446744073709551615",
        "99999999999999999999", "(", ")", "?", ":", "||", "&&", "==", "!=", "<", ">", "<=", ">=",
        "+", "-", "*", "/", "%", "!", " ", "\t", "=", "&", "|", "x", "\r" };
    std::string s;
    int shape = rnd(4);
    if (shape == 0) { // deep nesting
        int d = rnd(20000);
        const char *open = rnd(3) == 0 ? "!" : rnd(2) ? "(" : "n?";
        for (int i = 0; i < d; i++) s += open;
        s += "n%3";
        if (!strcmp(open, "(")) for (int i = 0; i < d; i++) s += ")";
        if (!strcmp(open, "n?")) for (int i = 0; i < d; i++) s += ":1";
    } else if (shape == 1) { // long flat chains
        int d = rnd(5000);
        for (int i = 0; i < d; i++) { s += "n"; s += toks[13 + rnd(13)]; }
        s += "1";
    } else {
        int len = rnd(shape == 2 ? 12 : 200);
        for (int i = 0; i < len; i++) s += toks[rnd(sizeof(toks) / sizeof(*toks))];
    }
    s += rnd(4) ? ";" : "";
    return s;
}

void exercise_plural(const std::string &e)
{
    osv_plural p{};
    if (osv_plural_compile(e.c_str(), &p)) {
        p.nplurals = 1 + rnd(6);
        for (unsigned long n : { 0UL, 1UL, 2UL, 3UL, 7UL, 1000000UL, ~0UL, ~0UL - 1 }) {
            unsigned long r = osv_plural_index(&p, n);
            if (r >= p.nplurals) {
                fprintf(stderr, "BUG: index %lu >= nplurals %lu\n", r, p.nplurals);
                abort();
            }
        }
        osv_plural_free(&p);
    }
}

int failures;

void expect_reject(std::vector<char> v, const char *what)
{
    std::vector<char> copy(v);
    osv_mo m;
    bool ok = !osv_mo_open(copy.data(), copy.size(), &m);
    if (!ok) {
        // accepted: then it must find nothing at all
        size_t len;
        uint32_t idx;
        ok = !osv_mo_find(&m, "alpha", &len, &idx) && !osv_mo_find(&m, "", &len, &idx);
    }
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    failures += !ok;
    exercise(copy.data(), copy.size());
}

void put32(std::vector<char> &v, size_t off, uint32_t w)
{
    memcpy(&v[off], &w, 4);
}

// The harness must catch what the checks prevent.
int self_test(char which)
{
    // a: read a string the tables point past the end of a 32-byte buffer.
    // b: evaluate a parse that never returns (unbounded recursion stands in
    //    for a parser without its depth limit) - here: index a NUL-less buffer.
    std::vector<char> v(32, 'x');
    char *buf = static_cast<char *>(malloc(v.size()));
    memcpy(buf, v.data(), v.size());
    if (which == 'a') {
        size_t len = strlen(buf); // no NUL inside: ASan heap-buffer-overflow
        printf("len %zu\n", len);
    } else {
        touch(buf + 30, 4, buf, v.size()); // outside: the harness's own BUG abort
    }
    free(buf);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "--self-test")) {
        return self_test(argc > 2 ? argv[2][0] : 'a');
    }
    if (argc < 4) {
        fprintf(stderr, "usage: %s <iterations> <seed> <file.mo>...\n", argv[0]);
        return 2;
    }
    long iters = atol(argv[1]);
    rng.seed(strtoull(argv[2], nullptr, 10));
    std::vector<std::vector<char>> seeds;
    for (int i = 3; i < argc; i++) {
        auto v = read_file(argv[i]);
        osv_mo m;
        bool ok = !v.empty() && osv_mo_open(v.data(), v.size(), &m);
        size_t len;
        uint32_t idx;
        ok = ok && osv_mo_find(&m, "", &len, &idx);
        printf("%s: seed %s accepted, header found\n", ok ? "PASS" : "FAIL", argv[i]);
        failures += !ok;
        if (ok) {
            seeds.push_back(v);
        }
    }
    if (seeds.empty()) {
        return 1;
    }
    // deterministic negatives on the first seed
    const auto &s0 = seeds[0];
    size_t sz = s0.size();
    for (size_t t : { 0UL, 4UL, 27UL }) {
        expect_reject(std::vector<char>(s0.begin(), s0.begin() + t), ("truncated to " + std::to_string(t)).c_str());
    }
    { auto v = s0; put32(v, 0, 0x12345678); expect_reject(v, "bad magic"); }
    { auto v = s0; put32(v, 4, 0x20000); expect_reject(v, "major revision 2"); }
    { auto v = s0; put32(v, 8, 0xffffffffu); expect_reject(v, "count 2^32-1"); }
    { auto v = s0; put32(v, 8, uint32_t(sz)); expect_reject(v, "count = size"); }
    { auto v = s0; put32(v, 12, uint32_t(sz)); expect_reject(v, "originals offset = size"); }
    { auto v = s0; put32(v, 12, 0xfffffff8u); expect_reject(v, "originals offset wraps"); }
    { auto v = s0; put32(v, 16, uint32_t(sz - 4)); expect_reject(v, "translations offset past end"); }
    { auto v = s0; put32(v, 20, 0x40000000u); expect_reject(v, "hash size huge"); }
    { auto v = s0; put32(v, 24, uint32_t(sz)); put32(v, 20, 7); expect_reject(v, "hash offset past end"); }
    { // every string offset just past the end: found nothing, read nothing
        auto v = s0;
        osv_mo m;
        osv_mo_open(v.data(), v.size(), &m);
        for (uint32_t i = 0; i < m.n; i++) {
            put32(v, m.otab + 8ull * i + 4, uint32_t(sz - 1));
            put32(v, m.otab + 8ull * i, 5);
        }
        expect_reject(v, "every original past the end");
    }
    { // unterminated: the last byte is not NUL and every string runs into it
        auto v = s0;
        for (size_t i = 28; i < v.size(); i++) if (!v[i]) v[i] = 'u';
        expect_reject(v, "no NUL anywhere after the header");
    }
    // plural negatives: depth limits and bad syntax rejected; division by zero handled
    struct { std::string e; bool ok; const char *what; } pl[] = {
        { std::string(9996, '(') + "n" + std::string(9996, ')') + ";", true, "9996 parens accepted" },
        { std::string(9997, '(') + "n" + std::string(9997, ')') + ";", false, "9997 parens rejected" },
        { std::string(100000, '(') + "n" + std::string(100000, ')') + ";", false, "100000 parens rejected" },
        { std::string(200000, '!') + "n;", false, "200000 ! rejected" },
        { "n+;", false, "n+ rejected" }, { "n=1;", false, "n=1 rejected" }, { "(n;", false, "( rejected" },
        { "n?1;", false, "?: without : rejected" }, { ";", false, "empty rejected" },
        { "n/0;", true, "n/0 parses" },
    };
    for (auto &c : pl) {
        osv_plural p{};
        bool got = osv_plural_compile(c.e.c_str(), &p);
        if (got) {
            p.nplurals = 2;
            sink += osv_plural_index(&p, 3);
            osv_plural_free(&p);
        }
        printf("%s: plural %s\n", got == c.ok ? "PASS" : "FAIL", c.what);
        failures += got != c.ok;
    }
    printf("DETERMINISTIC: %d failures\n", failures);
    long accepted = 0;
    for (long it = 0; it < iters; it++) {
        if (it % 4 == 3) {
            exercise_plural(random_expr());
            continue;
        }
        auto v = seeds[rnd(seeds.size())];
        mutate(v);
        char *buf = static_cast<char *>(malloc(v.size() ? v.size() : 1));
        memcpy(buf, v.data(), v.size());
        osv_mo m;
        accepted += osv_mo_open(buf, v.size(), &m);
        exercise(buf, v.size());
        free(buf);
    }
    printf("SUMMARY: %ld iterations (%ld .mo accepted after mutation), %d failures\n", iters, accepted, failures);
    return failures ? 1 : 0;
}
