/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_FS_9P_CODEC_HH
#define OSV_FS_9P_CODEC_HH

#include <cstddef>
#include <cstdint>
#include <vector>

// Generic 9P frame envelope: size[4] type[1] tag[2] body, little endian,
// where size counts the seven header bytes (Plan 9 intro(5), diod
// protocol.md). This layer bounds and copies exactly one complete frame. It
// does not validate operation-specific .L bodies, map errno values or match
// replies to outstanding requests; those belong to the session layer.
namespace p9 {

using bytes = std::vector<uint8_t>;

struct frame {
    uint8_t type;
    uint16_t tag;
    bytes body;
};

// msize is the negotiated (or, before negotiation, offered) bound; 65536 is
// the approved local ceiling.
//
// Decode exactly one complete frame occupying len bytes at p (any
// alignment). Returns 0; EINVAL if msize is outside [7, 65536]; EPROTO
// unless len equals the advertised size and 7 <= size <= msize (truncated,
// trailing or coalesced input included); ENOMEM. out is unchanged on error.
// Not an incremental stream parser: the transport assembles whole frames.
int decode(const uint8_t* p, size_t len, uint32_t msize, frame& out);

// Encode f into out. Returns 0; EINVAL if msize is outside [7, 65536];
// EMSGSIZE if 7 + body size exceeds msize; ENOMEM. out is unchanged on error.
int encode(const frame& f, uint32_t msize, bytes& out);

// Both functions allocate and may sleep: do not call them from interrupt,
// preempt-disabled or completion-callback context.

}

#endif
