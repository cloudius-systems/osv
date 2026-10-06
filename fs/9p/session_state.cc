/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Rules from published protocol descriptions only: Plan 9 intro(5) and
// flush(5) https://9fans.github.io/plan9port/man/man9/ and diod protocol.md
// (commit 481911e83337e67b60e039a6e950e532d4681805).

#include "session_state.hh"

#include <algorithm>
#include <cerrno>

namespace p9 {

int tag_table::take(uint16_t& tag)
{
    // At most data_tags + 1 numbers are held, so a free one is found within
    // that many steps; notag is skipped and never issued.
    for (;;) {
        uint16_t t = _next++;
        if (t != notag && !_held[t]) {
            _held[t] = true;
            tag = t;
            return 0;
        }
    }
}

int tag_table::acquire(uint16_t& tag)
{
    if (_data >= data_tags) {
        return EAGAIN;
    }
    take(tag);
    ++_data;
    return 0;
}

int tag_table::acquire_flush(uint16_t& tag)
{
    if (_flush_held) {
        return EAGAIN;
    }
    take(tag);
    _flush_held = true;
    _flush_tag = tag;
    return 0;
}

int tag_table::release(uint16_t tag)
{
    if (!held(tag)) {
        return EINVAL;
    }
    _held[tag] = false;
    if (_flush_held && tag == _flush_tag) {
        _flush_held = false;
        _flush_tag = notag;
    } else {
        --_data;
    }
    return 0;
}

int fid_table::reserve(uint32_t& fid)
{
    if (_used >= max_fids) {
        return ENFILE;
    }
    for (;;) {
        uint32_t f = _next;
        _next = (_next + 1) % max_fids;
        if (_state[f] == state::free) {
            _state[f] = state::reserved;
            ++_used;
            fid = f;
            return 0;
        }
    }
}

int fid_table::make_live(uint32_t fid)
{
    if (get(fid) != state::reserved) {
        return EINVAL;
    }
    _state[fid] = state::live;
    return 0;
}

int fid_table::release(uint32_t fid)
{
    if (get(fid) == state::free) {
        return EINVAL;
    }
    _state[fid] = state::free;
    --_used;
    return 0;
}

fid_table::state fid_table::get(uint32_t fid) const
{
    return fid < max_fids ? _state[fid] : state::free;
}

int budget::charge(size_t n)
{
    if (n > _ceiling - _used) {
        return ENOMEM;
    }
    _used += n;
    return 0;
}

int budget::refund(size_t n)
{
    if (n > _used) {
        return EINVAL;
    }
    _used -= n;
    return 0;
}

namespace {

constexpr size_t qid_size = 13; // type[1] version[4] path[8]

uint32_t get_u32(const uint8_t* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16
        | uint32_t(p[3]) << 24;
}

uint16_t get_u16(const uint8_t* p)
{
    return uint16_t(p[0] | p[1] << 8);
}

}

int check_version(const frame& r, uint32_t offered, uint32_t& msize)
{
    // Rversion: msize[4] version[s]; s is len[2] then len bytes, no NUL.
    static const char dialect[] = "9P2000.L";
    const size_t n = sizeof(dialect) - 1;
    const bytes& b = r.body;
    if (r.type != Rversion || r.tag != notag || b.size() < 6
        || b.size() != 6 + size_t(get_u16(&b[4]))) {
        return EPROTO;
    }
    if (b.size() != 6 + n || !std::equal(dialect, dialect + n, &b[6])) {
        return EPROTONOSUPPORT;
    }
    uint32_t m = get_u32(&b[0]);
    if (m < min_msize || m > offered) {
        return EPROTO;
    }
    msize = m;
    return 0;
}

int map_lerror(uint32_t ecode)
{
    // Linux asm-generic errno numbers 1..133; 41 and 58 are unassigned.
    // OSv's public errno.h uses the same numbering.
    if (ecode == 0) {
        return EPROTO;
    }
    if (ecode > 133 || ecode == 41 || ecode == 58) {
        return EIO;
    }
    return int(ecode);
}

int check_reply(const expect& e, const frame& r, int& err)
{
    if (r.tag != e.tag) {
        return EPROTO;
    }
    const bytes& b = r.body;
    if (r.type == Rlerror) {
        // Tflush must be answered by Rflush (flush(5)).
        if (e.type == Tflush || b.size() != 4) {
            return EPROTO;
        }
        err = map_lerror(get_u32(b.data()));
        return 0;
    }
    if (r.type != uint8_t(e.type + 1)) {
        return EPROTO; // includes legacy Rerror
    }
    bool ok;
    switch (e.type) {
    case Tattach:
        ok = b.size() == qid_size;
        break;
    case Tlopen:
        ok = b.size() == qid_size + 4;
        break;
    case Tclunk:
    case Tflush:
        ok = b.empty();
        break;
    case Twrite:
        ok = b.size() == 4 && get_u32(b.data()) <= e.limit;
        break;
    case Tread:
        ok = b.size() >= 4 && get_u32(b.data()) <= e.limit
            && b.size() - 4 == get_u32(b.data());
        break;
    case Twalk:
        ok = b.size() >= 2 && get_u16(b.data()) <= e.limit
            && b.size() == 2 + qid_size * get_u16(b.data());
        break;
    default:
        return EINVAL;
    }
    if (!ok) {
        return EPROTO;
    }
    err = 0;
    return 0;
}

}
