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

}
