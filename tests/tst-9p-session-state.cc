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
#include <cstring>
#include <set>

using p9::bytes;
using p9::frame;

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

static bytes le32(uint32_t v)
{
    return {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
}

static bytes cat(bytes a, const bytes& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// Rversion body: msize[4] version[s], s = len[2] then bytes.
static frame rversion(uint32_t msize, const char* v, uint16_t tag = p9::notag)
{
    size_t n = std::strlen(v);
    bytes b = cat(le32(msize), {uint8_t(n), uint8_t(n >> 8)});
    b.insert(b.end(), v, v + n);
    return {p9::Rversion, tag, b};
}

static void version_fails(const frame& r, uint32_t offered, int expected,
                          const char* name)
{
    uint32_t msize = 0x5a5a;
    require(p9::check_version(r, offered, msize) == expected, name);
    require(msize == 0x5a5a, name);
}

static void version()
{
    uint32_t msize = 0;
    require(p9::check_version(rversion(65536, "9P2000.L"), 65536, msize) == 0
            && msize == 65536, "Rversion at offered msize");
    require(p9::check_version(rversion(8192, "9P2000.L"), 65536, msize) == 0
            && msize == 8192, "server lowers msize");
    require(p9::check_version(rversion(p9::min_msize, "9P2000.L"), 65536,
                              msize) == 0 && msize == p9::min_msize,
            "msize at minimum");
    version_fails(rversion(65537, "9P2000.L"), 65536, EPROTO,
                  "msize above offer");
    version_fails(rversion(8193, "9P2000.L"), 8192, EPROTO,
                  "msize above smaller offer");
    version_fails(rversion(p9::min_msize - 1, "9P2000.L"), 65536, EPROTO,
                  "msize below minimum");
    version_fails(rversion(0, "9P2000.L"), 65536, EPROTO, "msize zero");
    for (const char* v : {"9P2000", "9P2000.u", "unknown", "9P2000.l",
                          "9P2000.L.x", "9P2000.", ""}) {
        version_fails(rversion(65536, v), 65536, EPROTONOSUPPORT,
                      "downgraded or foreign dialect refused");
    }
    frame nul = rversion(65536, "9P2000.L");
    nul.body[4] = 9;
    nul.body.push_back(0);
    version_fails(nul, 65536, EPROTONOSUPPORT, "NUL-suffixed dialect refused");
    version_fails(rversion(65536, "9P2000.L", 0), 65536, EPROTO,
                  "Rversion tag must be NOTAG");
    frame wrong = rversion(65536, "9P2000.L");
    wrong.type = p9::Rlerror;
    version_fails(wrong, 65536, EPROTO, "Rversion type required");
    wrong.type = p9::Rerror;
    version_fails(wrong, 65536, EPROTO, "legacy Rerror to Tversion");
    frame trailing = rversion(65536, "9P2000.L");
    trailing.body.push_back('x');
    version_fails(trailing, 65536, EPROTO, "trailing byte after version");
    frame shortstr = rversion(65536, "9P2000.L");
    shortstr.body.pop_back();
    version_fails(shortstr, 65536, EPROTO, "string overruns body");
    for (size_t n = 0; n < 6; n++) {
        version_fails({p9::Rversion, p9::notag, bytes(n, 0)}, 65536, EPROTO,
                      "truncated Rversion");
    }
    std::puts("PASS: Rversion validation");
}

static void lerror()
{
    // Linux asm-generic errno values 1..133 except the unused 41 and 58.
    for (uint32_t e = 1; e <= 133; e++) {
        int want = (e == 41 || e == 58) ? EIO : int(e);
        require(p9::map_lerror(e) == want, "Linux errno passes through");
    }
    require(p9::map_lerror(ENOENT) == ENOENT && ENOENT == 2, "ENOENT");
    require(p9::map_lerror(EPROTO) == EPROTO && EPROTO == 71, "EPROTO");
    require(p9::map_lerror(0) == EPROTO, "zero ecode is a protocol error");
    for (uint32_t e : {134u, 512u, 0x7fffffffu, 0x80000000u, 0xffffffffu}) {
        require(p9::map_lerror(e) == EIO, "unknown ecode maps to EIO");
    }
    std::puts("PASS: Rlerror mapping");
}

static const bytes qid{0x80, 1,2,3,4, 5,6,7,8,9,10,11,12};

static void reply_ok(const p9::expect& e, const frame& r, int want,
                     const char* name)
{
    int err = 0x5a;
    require(p9::check_reply(e, r, err) == 0 && err == want, name);
}

static void reply_bad(const p9::expect& e, const frame& r, const char* name)
{
    int err = 0x5a;
    require(p9::check_reply(e, r, err) == EPROTO, name);
}

// Every proper prefix and one-byte extension of a valid body is malformed.
static void lengths(const p9::expect& e, const frame& good, const char* name,
                    int want = 0)
{
    reply_ok(e, good, want, name);
    for (size_t n = 0; n < good.body.size(); n++) {
        reply_bad(e, {good.type, good.tag,
                      bytes(good.body.begin(), good.body.begin() + n)}, name);
    }
    reply_bad(e, {good.type, good.tag, cat(good.body, {0})}, name);
}

static void replies()
{
    const uint16_t t = 9;
    lengths({p9::Tattach, t, 0}, {p9::Rattach, t, qid}, "Rattach qid[13]");
    lengths({p9::Tlopen, t, 0}, {p9::Rlopen, t, cat(qid, le32(8192))},
            "Rlopen qid[13] iounit[4]");
    lengths({p9::Tclunk, t, 0}, {p9::Rclunk, t, {}}, "Rclunk empty");
    lengths({p9::Tflush, t, 0}, {p9::Rflush, t, {}}, "Rflush empty");
    lengths({p9::Twrite, t, 100}, {p9::Rwrite, t, le32(100)}, "Rwrite count");
    lengths({p9::Tread, t, 100}, {p9::Rread, t, cat(le32(3), {1, 2, 3})},
            "Rread count data");
    lengths({p9::Twalk, t, 2}, {p9::Rwalk, t, cat(cat({2, 0}, qid), qid)},
            "Rwalk nwqid qids");
    lengths({p9::Twalk, t, 2}, {p9::Rwalk, t, cat({1, 0}, qid)},
            "Rwalk partial");
    reply_ok({p9::Twalk, t, 0}, {p9::Rwalk, t, {0, 0}}, 0, "Rwalk clone");
    reply_ok({p9::Tread, t, 100}, {p9::Rread, t, le32(0)}, 0, "Rread EOF");
    reply_ok({p9::Twrite, t, 100}, {p9::Rwrite, t, le32(0)}, 0,
             "Rwrite zero");

    reply_bad({p9::Twalk, t, 1}, {p9::Rwalk, t, cat(cat({2, 0}, qid), qid)},
              "Rwalk nwqid above nwname");
    reply_bad({p9::Tread, t, 2}, {p9::Rread, t, cat(le32(3), {1, 2, 3})},
              "Rread count above requested");
    reply_bad({p9::Tread, t, 100}, {p9::Rread, t, cat(le32(0xffffffff), {1})},
              "Rread huge count");
    reply_bad({p9::Twrite, t, 99}, {p9::Rwrite, t, le32(100)},
              "Rwrite count above requested");

    // Type and tag pairing.
    reply_bad({p9::Tattach, t, 0}, {p9::Rattach, t + 1, qid}, "wrong tag");
    reply_bad({p9::Tattach, t, 0}, {p9::Rlopen, t, cat(qid, le32(0))},
              "wrong reply type");
    reply_bad({p9::Tattach, t, 0}, {p9::Tattach, t, qid}, "T as reply");
    reply_bad({p9::Tclunk, t, 0}, {p9::Rerror, t, {0, 0}},
              "legacy Rerror after .L negotiation");
    reply_bad({p9::Tclunk, t, 0}, {p9::Rerror, t, cat({2, 0, 'n', 'o'},
                                                     le32(ENOENT))},
              "legacy 9P2000.u Rerror refused");
    // Same 4-byte length as Rlerror, so only the type can reject it.
    reply_bad({p9::Tclunk, t, 0}, {p9::Rerror, t, {2, 0, 'n', 'o'}},
              "4-byte legacy Rerror refused");

    // Rlerror ecode[4] answers any request except Tflush.
    for (uint8_t ty : {p9::Tattach, p9::Twalk, p9::Tlopen, p9::Tread,
                       p9::Twrite, p9::Tclunk}) {
        p9::expect e{ty, t, 1};
        lengths(e, {p9::Rlerror, t, le32(ENOENT)}, "Rlerror ecode[4]", ENOENT);
        reply_ok(e, {p9::Rlerror, t, le32(ENOENT)}, ENOENT, "Rlerror errno");
        reply_ok(e, {p9::Rlerror, t, le32(0)}, EPROTO, "Rlerror zero");
        reply_ok(e, {p9::Rlerror, t, le32(9999)}, EIO, "Rlerror unknown");
        reply_bad(e, {p9::Rlerror, t + 1, le32(ENOENT)}, "Rlerror wrong tag");
    }
    reply_bad({p9::Tflush, t, 0}, {p9::Rlerror, t, le32(ENOENT)},
              "Tflush requires Rflush, not Rlerror");

    int err;
    require(p9::check_reply({p9::Tversion, p9::notag, 0},
                            rversion(65536, "9P2000.L"), err) == EINVAL,
            "Tversion goes through check_version");
    require(p9::check_reply({200, t, 0}, {201, t, {}}, err) == EINVAL,
            "unhandled request type");
    std::puts("PASS: reply validation");
}

int main()
{
    tags();
    fids();
    accounting();
    version();
    lerror();
    replies();
    std::printf("SUMMARY: %u session-state checks, 0 failures\n", checks);
    return 0;
}
