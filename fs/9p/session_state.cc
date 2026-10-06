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
#include <utility>

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

void request_table::complete(slot& s, int err)
{
    s.completed = true;
    s.err = err;
    ++_completions;
}

void request_table::retire_tag(slot& s)
{
    if (s.tag_live) {
        _tags.release(s.e.tag);
        s.tag_live = false;
    }
}

void request_table::maybe_free(unsigned id)
{
    slot& s = _slot[id];
    if (s.used && !s.tag_live && !s.buffer_live && s.collected) {
        s = slot{};
    }
}

int request_table::protocol_error()
{
    fail(EPROTO);
    return EPROTO;
}

int request_table::admit(uint8_t type, uint32_t limit, size_t bytes,
                         unsigned& id, uint16_t& tag)
{
    if (_dead) {
        return _dead;
    }
    switch (type) {
    case Tattach: case Twalk: case Tlopen: case Tread: case Twrite:
    case Tclunk:
        break;
    default:
        return EINVAL;
    }
    unsigned i = 0;
    while (i < data_tags && _slot[i].used) {
        ++i;
    }
    if (i == data_tags) {
        return EAGAIN;
    }
    if (int rc = _budget.charge(bytes)) {
        return rc;
    }
    // Cannot fail: every held data tag belongs to a used slot, and one slot
    // is free. (The converse does not hold: a slot may outlive its tag.)
    uint16_t t = notag;
    _tags.acquire(t);
    slot& s = _slot[i];
    s.used = s.tag_live = s.buffer_live = true;
    s.charge = bytes;
    s.e = {type, t, limit};
    id = i;
    tag = t;
    return 0;
}

int request_table::cancel(unsigned id, int err, uint16_t& flushtag)
{
    if (_dead) {
        return _dead;
    }
    // err 0 would make a canceled request look like a success with no reply.
    if (id >= data_tags || !_slot[id].used || err == 0) {
        return EINVAL;
    }
    slot& s = _slot[id];
    if (s.completed || s.flushing) {
        return EALREADY;
    }
    slot& f = _slot[flush_id];
    uint16_t t;
    if (f.used || _tags.acquire_flush(t)) {
        return EAGAIN;
    }
    // The flush has no caller: it is complete and collected from the start
    // and is freed once Rflush retires its tag and the transport its buffer.
    f.used = f.tag_live = f.buffer_live = true;
    f.completed = f.collected = true;
    f.e = {Tflush, t, 0};
    s.flushing = true;
    s.cancel_err = err;
    _flush_target = id;
    flushtag = t;
    return 0;
}

int request_table::received(unsigned id, frame r)
{
    if (_dead) {
        return _dead;
    }
    // Match by the buffer the reply arrived in, not by tag alone: a late
    // original whose tag Rflush retired and admit() reissued would
    // otherwise go to the tag's new owner. check_reply() checks the tag.
    if (id > flush_id || !_slot[id].tag_live) {
        return protocol_error(); // an original after Rflush, or bad id
    }
    slot& s = _slot[id];
    int err;
    if (s.replied || check_reply(s.e, r, err)) {
        return protocol_error();
    }
    s.replied = true;
    if (id == flush_id) {
        // Rflush: the server sends nothing more on oldtag. If the original
        // reply has not arrived, the request completes as canceled. Its
        // buffer stays owned until the transport retires it.
        slot& t = _slot[_flush_target];
        if (!t.replied) {
            complete(t, t.cancel_err);
        }
        t.flushing = false;
        retire_tag(t);
        retire_tag(s);
        maybe_free(_flush_target);
        maybe_free(flush_id);
        return 0;
    }
    // An original reply is honored exactly once; while a flush is
    // outstanding its tag stays held until Rflush.
    s.reply = std::move(r);
    complete(s, err);
    if (!s.flushing) {
        retire_tag(s);
    }
    maybe_free(id);
    return 0;
}

int request_table::retired(unsigned id)
{
    if (id > flush_id || !_slot[id].buffer_live) {
        return EINVAL;
    }
    slot& s = _slot[id];
    s.buffer_live = false;
    _budget.refund(s.charge);
    s.charge = 0;
    maybe_free(id);
    return 0;
}

int request_table::collect(unsigned id, int& err, frame& reply)
{
    if (id >= data_tags || !_slot[id].used || _slot[id].collected) {
        return EINVAL;
    }
    slot& s = _slot[id];
    if (!s.completed) {
        return EINPROGRESS;
    }
    err = s.err;
    reply = std::move(s.reply);
    s.reply = frame{};
    s.collected = true;
    maybe_free(id);
    return 0;
}

void request_table::fail(int err)
{
    if (_dead) {
        return;
    }
    _dead = err;
    for (unsigned id = 0; id <= flush_id; id++) {
        slot& s = _slot[id];
        if (s.used && !s.completed) {
            complete(s, err);
        }
        s.flushing = false;
        retire_tag(s);
        maybe_free(id);
    }
}

}
