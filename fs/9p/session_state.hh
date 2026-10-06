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
