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
    lengths({p9::Tgetattr, t, 0}, {p9::Rgetattr, t, bytes(153, 0)},
            "Rgetattr 153 bytes");
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
                       p9::Twrite, p9::Tclunk, p9::Tgetattr}) {
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

static frame rclunk(uint16_t tag) { return {p9::Rclunk, tag, {}}; }
static frame rflush(uint16_t tag) { return {p9::Rflush, tag, {}}; }

static unsigned admit(p9::request_table& rt, uint16_t& tag, size_t n = 100)
{
    unsigned id = 99;
    require(rt.admit(p9::Tclunk, 0, n, id, tag) == 0, "admit");
    require(id < p9::data_tags && rt.busy(id), "admitted slot busy");
    require(rt.tags().held(tag), "admitted tag held");
    return id;
}

static void collect_is(p9::request_table& rt, unsigned id, int want,
                       bool has_reply, const char* name)
{
    int err = 0x5a;
    frame r{0, 0, {1}};
    require(rt.collect(id, err, r) == 0 && err == want, name);
    require(has_reply ? r.type == p9::Rclunk : r.type == 0 && r.body.empty(),
            name);
    require(rt.collect(id, err, r) == EINVAL, "collect only once");
}

static void plain()
{
    p9::request_table rt;
    uint16_t tag;
    unsigned id = admit(rt, tag);
    require(rt.charged() == 100, "charged on admit");
    int err;
    frame r;
    require(rt.collect(id, err, r) == EINPROGRESS, "not complete yet");
    require(rt.received(id, rclunk(tag)) == 0, "reply accepted");
    require(!rt.tags().held(tag), "tag retired by its reply");
    require(rt.busy(id) && rt.charged() == 100,
            "buffer still owned until transport retires it");
    collect_is(rt, id, 0, true, "reply delivered");
    require(rt.busy(id), "collection does not free a live buffer");
    require(rt.retired(id) == 0, "transport retires buffer");
    require(!rt.busy(id) && rt.charged() == 0, "slot and charge returned");
    require(rt.retired(id) == EINVAL, "buffer retires once");
    require(rt.completions() == 1, "one completion");

    // Buffer retired before the reply is processed: tag still pins the slot.
    id = admit(rt, tag);
    require(rt.retired(id) == 0 && rt.busy(id), "tag pins slot");
    require(rt.received(id, rclunk(tag)) == 0, "reply after buffer retired");
    require(rt.busy(id), "uncollected result pins slot");
    collect_is(rt, id, 0, true, "reply after buffer retired delivered");
    require(!rt.busy(id) && rt.charged() == 0, "freed after all three");

    // Rlerror completes with the mapped errno.
    id = admit(rt, tag);
    require(rt.received(id, {p9::Rlerror, tag, {2, 0, 0, 0}}) == 0, "Rlerror");
    int e2;
    frame r2;
    require(rt.collect(id, e2, r2) == 0 && e2 == ENOENT, "Rlerror errno");
    require(rt.retired(id) == 0 && !rt.busy(id), "Rlerror slot freed");
    require(!rt.dead(), "session alive");
    unsigned bad = 77;
    for (uint8_t ty : {uint8_t(p9::Tversion), uint8_t(p9::Tflush), uint8_t(200)}) {
        require(rt.admit(ty, 0, 1, bad, tag) == EINVAL && bad == 77,
                "only validated request types admitted");
    }
    require(rt.admit(p9::Tgetattr, 0, 1, id, tag) == 0
            && rt.received(id, {p9::Rgetattr, tag, bytes(153, 0)}) == 0
            && rt.collect(id, e2, r2) == 0 && e2 == 0
            && r2.body.size() == 153 && rt.retired(id) == 0,
            "Tgetattr admitted and answered");
    require(rt.charged() == 0 && rt.tags().data_held() == 0, "nothing leaked");
    std::puts("PASS: plain request lifecycle");
}

static void admission()
{
    p9::request_table rt(1000);
    uint16_t tags[p9::data_tags];
    unsigned ids[p9::data_tags];
    for (unsigned i = 0; i < p9::data_tags; i++) {
        ids[i] = admit(rt, tags[i], 10);
    }
    unsigned id = 77;
    uint16_t tag = 77;
    require(rt.admit(p9::Tclunk, 0, 1, id, tag) == EAGAIN && id == 77
            && tag == 77, "33rd request refused");
    uint16_t ftag;
    require(rt.cancel(ids[3], ETIMEDOUT, ftag) == 0,
            "flush admitted with 32 data requests outstanding");
    require(rt.received(p9::request_table::flush_id, rflush(ftag)) == 0,
            "Rflush");
    require(rt.retired(p9::request_table::flush_id) == 0, "flush retired");
    require(rt.busy(ids[3]), "canceled slot pinned by its buffer");
    require(rt.admit(p9::Tclunk, 0, 1, id, tag) == EAGAIN,
            "still full until buffer retires");
    require(rt.retired(ids[3]) == 0, "retire canceled buffer");
    collect_is(rt, ids[3], ETIMEDOUT, false, "canceled result");
    require(rt.admit(p9::Tclunk, 0, 1001, id, tag) == ENOMEM,
            "charge over ceiling refused");
    require(rt.tags().data_held() == p9::data_tags - 1, "no tag leaked");
    require(rt.charged() == 310, "no charge leaked");
    require(rt.admit(p9::Tclunk, 0, 690, id, tag) == 0 && rt.charged() == 1000,
            "charge to the ceiling");
    std::puts("PASS: admission and allocation ceiling");
}

static void original_then_rflush()
{
    p9::request_table rt;
    uint16_t tag, ftag;
    unsigned id = admit(rt, tag);
    require(rt.cancel(id, ETIMEDOUT, ftag) == 0, "cancel sends Tflush");
    require(ftag != tag && rt.tags().flush_held(), "flush on reserved tag");
    uint16_t f2;
    require(rt.cancel(id, ETIMEDOUT, f2) == EALREADY, "cancel coalesced");
    uint16_t t2;
    unsigned id2 = admit(rt, t2);
    require(rt.cancel(id2, ETIMEDOUT, f2) == EAGAIN, "one flush at a time");
    require(rt.received(id, rclunk(tag)) == 0,
            "original before Rflush honored");
    require(rt.tags().held(tag), "oldtag held until Rflush");
    collect_is(rt, id, 0, true, "original success delivered once");
    require(rt.received(id, rclunk(tag)) == EPROTO && rt.dead() == EPROTO,
            "duplicate original fails session");
    int err;
    frame r;
    require(rt.collect(id2, err, r) == 0 && err == EPROTO,
            "bystander completed by session failure");
    require(rt.completions() == 2, "each request completed once");

    p9::request_table q;
    id = admit(q, tag);
    require(q.cancel(id, ETIMEDOUT, ftag) == 0, "cancel");
    require(q.received(id, rclunk(tag)) == 0, "original first");
    require(q.retired(id) == 0, "original buffer retired");
    collect_is(q, id, 0, true, "original result");
    require(q.busy(id) && q.tags().held(tag),
            "slot and oldtag pinned until Rflush");
    require(q.received(p9::request_table::flush_id, rflush(ftag)) == 0,
            "Rflush after original");
    require(!q.tags().held(tag) && !q.tags().flush_held(), "tags retired");
    require(!q.busy(id), "original slot freed");
    require(q.busy(p9::request_table::flush_id), "flush buffer still owned");
    require(q.retired(p9::request_table::flush_id) == 0, "flush retired");
    require(q.completions() == 1 && !q.dead(), "one completion, alive");
    require(q.charged() == 0, "charges returned");
    std::puts("PASS: original reply before Rflush");
}

static void rflush_then_original()
{
    p9::request_table rt;
    uint16_t tag, ftag;
    unsigned id = admit(rt, tag);
    require(rt.cancel(id, ETIMEDOUT, ftag) == 0, "cancel");
    require(rt.received(id, {p9::Rflush, tag, {}}) == EPROTO,
            "Rflush on the original tag is a protocol error");

    p9::request_table q;
    id = admit(q, tag);
    ftag = 0x5a5a;
    require(q.cancel(id, 0, ftag) == EINVAL && ftag == 0x5a5a
            && !q.tags().flush_held(), "zero cancel errno refused");
    require(q.cancel(id, EINTR, ftag) == 0, "cancel");
    require(q.received(p9::request_table::flush_id, rflush(ftag)) == 0,
            "Rflush first");
    require(!q.tags().held(tag), "oldtag retired by Rflush");
    require(q.busy(id) && q.charged() == 100,
            "Rflush does not retire the original buffer");
    collect_is(q, id, EINTR, false, "canceled once with cancel error");
    require(q.busy(id), "slot pinned by buffer after collection");
    require(q.retired(id) == 0 && !q.busy(id), "freed by buffer retirement");
    require(q.received(id, rclunk(tag)) == EPROTO && q.dead() == EPROTO,
            "late original after Rflush fails session");
    require(q.completions() == 1, "late original not delivered");

    // The late original arrives after its tag was reissued to another
    // slot: matched by id it fails the session, never completing the
    // tag's new owner.
    p9::request_table w;
    id = admit(w, tag);
    require(w.cancel(id, EINTR, ftag) == 0, "cancel");
    require(w.received(p9::request_table::flush_id, rflush(ftag)) == 0
            && w.retired(p9::request_table::flush_id) == 0, "Rflush");
    collect_is(w, id, EINTR, false, "canceled");
    require(w.busy(id), "original buffer not retired");
    uint16_t t2 = 0;
    unsigned id2 = 0;
    for (unsigned n = 0; n < 0x10000; n++) {
        id2 = admit(w, t2);
        if (t2 == tag) {
            break;
        }
        require(w.received(id2, rclunk(t2)) == 0 && w.retired(id2) == 0,
                "cycle a tag");
        collect_is(w, id2, 0, true, "cycled");
    }
    require(t2 == tag && id2 != id, "tag reissued to another slot");
    require(w.received(id, rclunk(tag)) == EPROTO && w.dead() == EPROTO,
            "late original after reissue fails session");
    collect_is(w, id2, EPROTO, false, "new owner gets the session error");

    p9::request_table x;
    id = admit(x, tag);
    require(x.cancel(id, EINTR, ftag) == 0, "cancel");
    require(x.received(p9::request_table::flush_id,
                       {p9::Rlerror, ftag, {4, 0, 0, 0}}) == EPROTO,
            "Rlerror to Tflush fails session");
    p9::request_table y;
    id = admit(y, tag);
    require(y.received(id, rclunk(tag)) == 0, "reply");
    require(y.cancel(id, EINTR, ftag) == EALREADY, "cancel after completion");
    require(!y.tags().flush_held(), "no flush for completed request");
    require(y.cancel(p9::data_tags, EINTR, ftag) == EINVAL
            && y.cancel(1000, EINTR, ftag) == EINVAL, "cancel bad id");
    std::puts("PASS: Rflush before original reply");
}

static void transport_failure()
{
    p9::request_table rt;
    uint16_t tags[4], ftag;
    unsigned ids[4];
    for (unsigned i = 0; i < 4; i++) {
        ids[i] = admit(rt, tags[i]);
    }
    require(rt.received(ids[0], rclunk(tags[0])) == 0, "one already answered");
    require(rt.cancel(ids[1], ETIMEDOUT, ftag) == 0, "one being flushed");
    rt.fail(ECONNRESET);
    require(rt.dead() == ECONNRESET, "session dead");
    rt.fail(EIO);
    require(rt.dead() == ECONNRESET, "first failure sticks");
    require(rt.completions() == 4, "every request completed exactly once");
    collect_is(rt, ids[0], 0, true, "answered result kept");
    for (unsigned i = 1; i < 4; i++) {
        collect_is(rt, ids[i], ECONNRESET, false, "failed with error");
        require(!rt.tags().held(tags[i]), "tags retired");
    }
    require(!rt.tags().flush_held(), "flush tag retired");
    for (unsigned i = 0; i < 4; i++) {
        require(rt.busy(ids[i]), "buffers still owned by transport");
        require(rt.retired(ids[i]) == 0, "retire on dead session");
    }
    require(rt.retired(p9::request_table::flush_id) == 0, "retire flush");
    require(rt.charged() == 0, "all charges returned");
    unsigned id;
    uint16_t tag;
    require(rt.admit(p9::Tclunk, 0, 1, id, tag) == ECONNRESET, "no admit");
    require(rt.cancel(ids[2], EINTR, ftag) == ECONNRESET, "no cancel");
    require(rt.received(ids[2], rclunk(tags[2])) == ECONNRESET,
            "reply dropped");
    require(rt.completions() == 4, "nothing completed twice");

    p9::request_table u;
    id = admit(u, tag);
    require(u.received(id, rclunk(tag + 1)) == EPROTO && u.dead() == EPROTO,
            "unknown tag fails session");
    collect_is(u, id, EPROTO, false, "outstanding failed by protocol error");
    std::puts("PASS: transport failure");
}

int main()
{
    tags();
    fids();
    accounting();
    version();
    lerror();
    replies();
    plain();
    admission();
    original_then_rflush();
    rflush_then_original();
    transport_failure();
    std::printf("SUMMARY: %u session-state checks, 0 failures\n", checks);
    return 0;
}
