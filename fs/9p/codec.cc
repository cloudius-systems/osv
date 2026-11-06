/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Layout from published protocol descriptions only (no implementation code):
// Plan 9 intro(5) https://9fans.github.io/plan9port/man/man9/intro.html and
// diod protocol.md (commit 481911e83337e67b60e039a6e950e532d4681805).
// Fields are explicit little-endian byte accesses; no packed casts, no
// alignment or host-endian assumptions.

#include "codec.hh"

#include <algorithm>
#include <cerrno>
#include <new>

namespace p9 {

namespace {

constexpr uint32_t header_size = 7; // size[4] type[1] tag[2]
constexpr uint32_t max_msize = 65536; // approved 64-KiB local ceiling

bool valid_msize(uint32_t msize)
{
    return msize >= header_size && msize <= max_msize;
}

uint32_t get_u32(const uint8_t* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16
        | uint32_t(p[3]) << 24;
}

void put_u32(uint8_t* p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

}

int decode(const uint8_t* p, size_t len, uint32_t msize, frame& out)
{
    if (!valid_msize(msize)) {
        return EINVAL;
    }
    // Check every bound before reading past the header or allocating.
    // size == len >= 7 also gives the protocol minimum size >= 7.
    if (len < header_size) {
        return EPROTO;
    }
    uint32_t size = get_u32(p);
    if (size != len || size > msize) {
        return EPROTO;
    }
    try {
        bytes body(p + header_size, p + size);
        out.type = p[4];
        out.tag = uint16_t(p[5] | p[6] << 8);
        out.body.swap(body);
    } catch (const std::bad_alloc&) {
        return ENOMEM;
    }
    return 0;
}

int encode(const frame& f, uint32_t msize, bytes& out)
{
    if (!valid_msize(msize)) {
        return EINVAL;
    }
    // Compare against the remaining room so the addition cannot overflow.
    if (f.body.size() > msize - header_size) {
        return EMSGSIZE;
    }
    uint32_t size = header_size + f.body.size();
    try {
        bytes wire(size);
        put_u32(wire.data(), size);
        wire[4] = f.type;
        wire[5] = f.tag;
        wire[6] = f.tag >> 8;
        std::copy(f.body.begin(), f.body.end(), wire.begin() + header_size);
        out.swap(wire);
    } catch (const std::bad_alloc&) {
        return ENOMEM;
    }
    return 0;
}

}
