/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Deterministic, single-threaded checks of the 9P session bookkeeping in
// fs/9p/session_state.{hh,cc}. Protocol facts: Plan 9 intro(5) and flush(5)
// (https://9fans.github.io/plan9port/man/man9/), diod protocol.md
// (481911e83337e67b60e039a6e950e532d4681805), Linux v6.12
// include/net/9p/9p.h for opcode numbers. No implementation code imported.
// Concurrency, deadlines and transports are not covered here.

#include <fs/9p/session_state.hh>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <set>

static unsigned checks;

static void require(bool ok, const char* name)
{
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s (check %u)\n", name, checks);
        std::exit(1);
    }
}

static void tags()
{
    p9::tag_table t;
    std::set<uint16_t> seen;
    for (unsigned i = 0; i < p9::data_tags; i++) {
        uint16_t tag = 0x1234;
        require(t.acquire(tag) == 0, "32 data tags admitted");
        require(tag != p9::notag, "data tag is not NOTAG");
        require(seen.insert(tag).second, "data tags distinct");
        require(t.held(tag), "acquired tag held");
    }
    uint16_t tag = 0x1234;
    require(t.acquire(tag) == EAGAIN && tag == 0x1234,
            "33rd data tag refused with EAGAIN, output untouched");
    uint16_t ftag;
    require(t.acquire_flush(ftag) == 0, "flush tag available when data full");
    require(ftag != p9::notag && !seen.count(ftag), "flush tag distinct");
    require(t.acquire_flush(tag) == EAGAIN, "second flush refused");
    require(t.acquire(tag) == EAGAIN, "flush does not free a data slot");
    require(t.release(p9::notag) == EINVAL, "NOTAG never releasable");
    require(!t.held(p9::notag), "NOTAG never held");

    // Retire one data tag: the slot reopens but the number is not reissued
    // while other numbers remain unused.
    uint16_t first = *seen.begin();
    require(t.release(first) == 0, "release held tag");
    require(!t.held(first), "released tag not held");
    require(t.release(first) == EINVAL, "double release refused");
    require(t.acquire(tag) == 0 && tag != first, "retired tag not reissued");
    require(!seen.count(tag) && tag != ftag, "fresh number issued");
    require(t.release(ftag) == 0 && !t.flush_held(), "flush released");
    require(t.acquire_flush(ftag) == 0, "flush slot reusable after release");
    require(t.release(ftag) == 0, "flush released again");

    // Cycle through the whole space: NOTAG never appears, no live tag is
    // ever reissued, and wrap-around reuses retired numbers.
    p9::tag_table w;
    uint16_t keep;
    require(w.acquire(keep) == 0, "hold one tag across wrap");
    bool wrapped = false;
    uint16_t prev = keep;
    for (unsigned i = 0; i < 3 * 0x10000; i++) {
        uint16_t x;
        int rc = (i % 7 == 0) ? w.acquire_flush(x) : w.acquire(x);
        require(rc == 0, "acquire during wrap");
        require(x != p9::notag, "NOTAG never issued across wrap");
        require(x != keep, "held tag never reissued across wrap");
        if (x < prev) {
            wrapped = true;
        }
        prev = x;
        require(w.release(x) == 0, "release during wrap");
    }
    require(wrapped, "cursor wrapped");
    require(w.held(keep) && w.data_held() == 1, "held tag survives wrap");
    std::puts("PASS: tag table");
}

static void fids()
{
    p9::fid_table f;
    std::set<uint32_t> seen;
    for (unsigned i = 0; i < p9::max_fids; i++) {
        uint32_t fid;
        require(f.reserve(fid) == 0, "4096 fids reservable");
        require(fid != p9::nofid, "fid is not NOFID");
        require(seen.insert(fid).second, "fids distinct");
        require(f.get(fid) == p9::fid_table::state::reserved, "reserved");
    }
    uint32_t fid = 77;
    require(f.reserve(fid) == ENFILE && fid == 77, "4097th fid refused");
    require(f.used() == p9::max_fids, "used counts reserved");
    require(f.get(p9::nofid) == p9::fid_table::state::free, "NOFID free");
    require(f.make_live(p9::nofid) == EINVAL, "NOFID never live");
    require(f.release(p9::nofid) == EINVAL, "NOFID never released");

    uint32_t a = *seen.begin();
    require(f.make_live(a) == 0, "reserved -> live");
    require(f.get(a) == p9::fid_table::state::live, "live");
    require(f.make_live(a) == EINVAL, "live -> live refused");
    require(f.release(a) == 0 && f.get(a) == p9::fid_table::state::free,
            "live released");
    require(f.release(a) == EINVAL, "double release refused");
    require(f.make_live(a) == EINVAL, "free -> live refused");
    uint32_t b = *seen.rbegin();
    require(f.release(b) == 0, "reserved released (failed create)");
    require(f.used() == p9::max_fids - 2, "used tracks releases");
    require(f.reserve(fid) == 0 && f.get(fid) == p9::fid_table::state::reserved,
            "slot reusable after release");
    std::puts("PASS: fid table");
}

static void accounting()
{
    p9::budget b;
    const size_t cap = p9::alloc_ceiling;
    require(cap == size_t(16) << 20, "16 MiB ceiling");
    require(b.charge(cap - 10) == 0, "charge below ceiling");
    require(b.charge(11) == ENOMEM && b.used() == cap - 10,
            "charge over ceiling refused, unchanged");
    require(b.charge(10) == 0 && b.used() == cap, "charge to exact ceiling");
    require(b.charge(1) == ENOMEM, "one over refused");
    require(b.refund(cap + 1) == EINVAL && b.used() == cap,
            "over-refund refused");
    require(b.refund(cap) == 0 && b.used() == 0, "refund returns all");
    require(b.charge(SIZE_MAX) == ENOMEM && b.used() == 0,
            "overflowing charge refused");
    require(b.charge(5) == 0 && b.charge(SIZE_MAX - 2) == ENOMEM
            && b.used() == 5, "wrapping charge refused");
    std::puts("PASS: allocation accounting");
}

int main()
{
    tags();
    fids();
    accounting();
    std::printf("SUMMARY: %u session-state checks, 0 failures\n", checks);
    return 0;
}
