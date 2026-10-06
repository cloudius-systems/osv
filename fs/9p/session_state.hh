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
    Rlerror = 7, Tlopen = 12, Rlopen = 13, Tversion = 100, Rversion = 101,
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

}

#endif
