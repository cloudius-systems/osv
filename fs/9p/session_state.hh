/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_FS_9P_SESSION_STATE_HH
#define OSV_FS_9P_SESSION_STATE_HH

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>

#include "codec.hh"

// Single-threaded, deterministic 9P2000.L session bookkeeping: tag and fid
// tables, allocation accounting, reply validation and the per-request state
// machine. No locks, threads or OS calls; the session layer serializes all
// access under its own mutex.
namespace p9 {

constexpr uint16_t notag = 0xffff;
constexpr uint32_t nofid = 0xffffffff;
constexpr unsigned data_tags = 32;     // admitted data RPCs
constexpr unsigned max_fids = 4096;    // live plus reserved
constexpr size_t alloc_ceiling = size_t(16) << 20;
// Smallest msize accepted from Rversion. Above the codec's 7-byte floor so
// that later I/O sizing (msize minus the 23-byte Twrite header) has room.
constexpr uint32_t min_msize = 4096;

// Message types used by this layer (numbers from Linux include/net/9p/9p.h).
enum : uint8_t {
    Rlerror = 7, Tlopen = 12, Rlopen = 13, Tgetattr = 24, Rgetattr = 25,
    Treaddir = 40, Rreaddir = 41,
    Tversion = 100, Rversion = 101,
    Tattach = 104, Rattach = 105, Rerror = 107, Tflush = 108, Rflush = 109,
    Twalk = 110, Rwalk = 111, Tread = 116, Rread = 117, Twrite = 118,
    Rwrite = 119, Tclunk = 120, Rclunk = 121,
};

// Validate the Rversion answering a Tversion that offered msize `offered`.
// Returns 0 and sets msize; EPROTONOSUPPORT unless the version string is
// exactly "9P2000.L"; EPROTO for a wrong type, a tag other than NOTAG, a
// malformed body, or msize outside [min_msize, offered]. msize is unchanged
// on error.
int check_version(const frame& r, uint32_t offered, uint32_t& msize);

// Map an Rlerror ecode to a local errno: Linux errno values pass through,
// 0 becomes EPROTO and anything else EIO.
int map_lerror(uint32_t ecode);

// What a reply is checked against: the request's type and tag, and for
// Twalk the number of names, for Tread/Twrite the byte count requested.
struct expect {
    uint8_t type;
    uint16_t tag;
    uint32_t limit;
};

// Validate reply r to request e. Returns 0 when r is well formed, with err
// 0 for the success reply or the mapped errno for an Rlerror. Returns EPROTO
// (fatal to the session) for a mismatched tag, a type other than e.type + 1
// or Rlerror (legacy Rerror included), an Rlerror answering Tflush, or a
// wrong length or count. EINVAL if e.type is not handled here.
int check_reply(const expect& e, const frame& r, int& err);

// Tags come from one rotating cursor over [0, notag), so a retired tag is
// not reissued until the cursor wraps and NOTAG is never issued. 32 data
// tags plus one flush tag may be held at once; the flush slot is reserved
// so data requests can never starve their own cancellation.
class tag_table {
public:
    // 0 and the tag, or EAGAIN when all data (or the flush) slots are held.
    int acquire(uint16_t& tag);
    int acquire_flush(uint16_t& tag);
    // 0, or EINVAL if the tag is not held. Callers release only on
    // protocol retirement (reply or Rflush), never on caller timeout.
    int release(uint16_t tag);
    bool held(uint16_t tag) const { return tag != notag && _held[tag]; }
    unsigned data_held() const { return _data; }
    bool flush_held() const { return _flush_held; }
private:
    int take(uint16_t& tag);
    std::bitset<0x10000> _held;
    uint16_t _next = 0;
    unsigned _data = 0;
    bool _flush_held = false;
    uint16_t _flush_tag = notag;
};

// Fids are numbers in [0, max_fids), far from NOFID. A fid is reserved
// when a Tattach/Twalk naming it as newfid is admitted, becomes live when the
// server confirms it, and is released exactly once after its clunk outcome
// (or failed creation) is resolved.
class fid_table {
public:
    enum class state : uint8_t { free, reserved, live };
    // 0 and the fid, or ENFILE when max_fids are reserved or live.
    int reserve(uint32_t& fid);
    // reserved -> live; EINVAL from any other state.
    int make_live(uint32_t fid);
    // reserved or live -> free; EINVAL if already free or out of range.
    int release(uint32_t fid);
    state get(uint32_t fid) const;
    unsigned used() const { return _used; }
private:
    std::array<state, max_fids> _state{};
    uint32_t _next = 0;
    unsigned _used = 0;
};

// Byte accounting against the session ceiling. Charged when a request's
// buffers are reserved and refunded when the transport retires them.
class budget {
public:
    explicit budget(size_t ceiling = alloc_ceiling) : _ceiling(ceiling) {}
    // 0, or ENOMEM if n would exceed the ceiling (overflow-safe).
    int charge(size_t n);
    // 0, or EINVAL if n exceeds what is charged.
    int refund(size_t n);
    size_t used() const { return _used; }
private:
    size_t _ceiling;
    size_t _used = 0;
};

// The request table: one slot per admitted data request plus one for the
// serialized flush. A slot has three independent retirements:
//   caller completion: result published once; collect() takes it once;
//   tag retirement: the server will send nothing more on the tag;
//   buffer retirement: the transport no longer references the buffers.
// The slot, and its charge, is reused only after all three. Replies are
// matched by id and must carry that slot's live tag; buffer retirement is
// by id, since a buffer can outlive the tag Rflush retired.
class request_table {
public:
    static constexpr unsigned flush_id = data_tags;
    explicit request_table(size_t ceiling = alloc_ceiling) : _budget(ceiling) {}
    // Admit a data request (type and limit as in expect) and charge bytes
    // for its buffers. 0 with id and tag; EAGAIN when 32 slots are busy;
    // ENOMEM over the ceiling; the session error once dead.
    int admit(uint8_t type, uint32_t limit, size_t bytes, unsigned& id,
              uint16_t& tag);
    // Cancel id. 0: send Tflush(oldtag) on flushtag as transport id
    // flush_id; err completes id if Rflush wins. EALREADY: already complete
    // or already being flushed, send nothing. EAGAIN: another flush is
    // outstanding. EINVAL: bad id or err 0. The session error once dead. The flush
    // is not charged, so a full budget cannot block cancellation.
    int cancel(unsigned id, int err, uint16_t& flushtag);
    // A decoded reply that arrived in id's buffer (flush_id for Rflush).
    // 0 if accepted. EPROTO for a protocol violation (a tag that is not
    // id's live tag, bad reply, duplicate, late original), which fails the
    // session. The session error, dropping r, once dead.
    int received(unsigned id, frame r);
    // The transport released id's buffers (flush_id for the flush). Refunds
    // the charge; allowed on a dead session. 0, or EINVAL if not held.
    int retired(unsigned id);
    // Caller completion. 0 once, with err (0 or errno) and the reply if one
    // completed it (else an empty frame); EINPROGRESS before completion;
    // EINVAL after collection or for a bad id.
    int collect(unsigned id, int& err, frame& reply);
    // Session failure: complete every uncompleted request with err, retire
    // all tags, refuse new work. Buffers still need retired(). Idempotent.
    void fail(int err);
    int dead() const { return _dead; }
    bool busy(unsigned id) const { return id <= flush_id && _slot[id].used; }
    const tag_table& tags() const { return _tags; }
    size_t charged() const { return _budget.used(); }
    unsigned completions() const { return _completions; }
private:
    struct slot {
        bool used = false;
        bool tag_live = false;
        bool buffer_live = false;
        bool completed = false;
        bool collected = false;
        bool replied = false;
        bool flushing = false;
        int err = 0;
        int cancel_err = 0;
        size_t charge = 0;
        expect e{};
        frame reply{};
    };
    void complete(slot& s, int err);
    void retire_tag(slot& s);
    void maybe_free(unsigned id);
    int protocol_error();
    std::array<slot, data_tags + 1> _slot{};
    unsigned _flush_target = 0;
    tag_table _tags;
    budget _budget;
    int _dead = 0;
    unsigned _completions = 0;
};

}

#endif
