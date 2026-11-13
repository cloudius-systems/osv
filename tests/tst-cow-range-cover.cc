// Self-check for the fork clone COW range membership optimization in
// core/mmu.cc: coalesce_cow_ranges() + range_covers() must return, for every
// address, exactly the same membership result as the original per-range linear
// scan (addr_is_* before the fix).  Random differential test.
//
// Build/run off-target (host toolchain), it mirrors the mmu.cc logic verbatim:
//   g++ -std=gnu++14 -O2 -o /tmp/t tst-cow-range-cover.cc && /tmp/t
//
// Copyright (C) 2026 Greg Burd <greg@burd.me>
//
// This work is open source software, licensed under the terms of the
// BSD license as described in the LICENSE file in the top-level directory.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <random>

struct cow_range { uintptr_t start, end; };

// --- verbatim copy of the two functions under test from core/mmu.cc ---
static void coalesce_cow_ranges(std::vector<cow_range> &v)
{
    if (v.size() < 2) return;
    std::sort(v.begin(), v.end(),
              [](const cow_range &a, const cow_range &b) {
                  return a.start < b.start;
              });
    size_t w = 0;
    for (size_t r = 1; r < v.size(); r++) {
        if (v[r].start <= v[w].end) {
            if (v[r].end > v[w].end) v[w].end = v[r].end;
        } else {
            v[++w] = v[r];
        }
    }
    v.resize(w + 1);
}

static bool range_covers(const std::vector<cow_range> *ranges, uintptr_t va)
{
    if (!ranges || ranges->empty()) return false;
    auto it = std::upper_bound(ranges->begin(), ranges->end(), va,
                               [](uintptr_t v, const cow_range &r) {
                                   return v < r.start;
                               });
    if (it == ranges->begin()) return false;
    --it;
    return va < it->end;
}

// --- the original linear scan it must match ---
static bool linear_covers(const std::vector<cow_range> &v, uintptr_t va)
{
    for (auto &r : v) {
        if (va >= r.start && va < r.end) return true;
    }
    return false;
}

int main()
{
    std::mt19937_64 rng(12345);
    int iters = 20000;
    long checks = 0;
    for (int it = 0; it < iters; it++) {
        // Build a random range set: empty-capable ranges (start==end) excluded
        // like clone_pt_level0 only ever adds non-empty stacks/vmas, plus
        // deliberately overlapping and adjacent ranges to stress the merge.
        std::vector<cow_range> raw;
        int n = rng() % 12;              // 0..11 ranges, incl. empty set
        uintptr_t space = 1 + (rng() % 2000);
        for (int i = 0; i < n; i++) {
            uintptr_t s = rng() % space;
            uintptr_t len = 1 + (rng() % 50); // non-empty
            raw.push_back({s, s + len});
        }
        std::vector<cow_range> coalesced = raw;
        coalesce_cow_ranges(coalesced);

        // Invariant: coalesced is sorted and strictly disjoint (no touching).
        for (size_t i = 1; i < coalesced.size(); i++) {
            if (!(coalesced[i-1].end < coalesced[i].start)) {
                fprintf(stderr, "FAIL: not disjoint after coalesce at iter %d\n", it);
                return 1;
            }
        }
        // Differential: every address in [0, space+60) must agree.
        for (uintptr_t va = 0; va < space + 60; va++) {
            bool want = linear_covers(raw, va);
            bool got  = range_covers(&coalesced, va);
            checks++;
            if (want != got) {
                fprintf(stderr, "FAIL iter %d va=%lu want=%d got=%d (n=%d)\n",
                        it, (unsigned long)va, want, got, n);
                return 1;
            }
        }
    }
    // Explicit edge cases.
    {
        std::vector<cow_range> e; coalesce_cow_ranges(e);
        if (range_covers(&e, 0) || range_covers(nullptr, 0)) {
            fprintf(stderr, "FAIL: empty/null set must cover nothing\n"); return 1;
        }
        // single range boundaries [10,20): 9 no, 10..19 yes, 20 no
        std::vector<cow_range> s{{10,20}}; coalesce_cow_ranges(s);
        if (range_covers(&s,9) || !range_covers(&s,10) || !range_covers(&s,19)
            || range_covers(&s,20)) {
            fprintf(stderr, "FAIL: boundary test\n"); return 1;
        }
        // fully-nested: [0,300) swallows [50,60)
        std::vector<cow_range> nest{{0,300},{50,60}}; coalesce_cow_ranges(nest);
        if (nest.size()!=1 || !range_covers(&nest,150)) {
            fprintf(stderr, "FAIL: nested merge\n"); return 1;
        }
    }
    printf("ok: %d iters, %ld differential checks, all agree\n", iters, checks);
    return 0;
}
