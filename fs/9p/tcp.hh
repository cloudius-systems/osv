/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_FS_9P_TCP_HH
#define OSV_FS_9P_TCP_HH

#include <cstddef>
#include <cstdint>
#include <memory>

#include "session.hh"

struct sockaddr;

// 9P over a byte stream (TCP). Sends are serialized so complete frames
// never interleave; one receiver thread assembles each reply frame,
// bounded by its request's rx buffer, and hands it to the session.
namespace p9 {

// Nonblocking byte stream under the transport. The socket implementation
// lives in tcp.cc; tests supply scripted streams to inject partial
// transfers, EINTR, EAGAIN, end of stream and resets.
class stream {
public:
    virtual ~stream() {}
    // Move at most n bytes and set done to what moved, also when returning
    // an error. 0; EAGAIN if nothing can move now; EINTR; any other errno
    // is fatal. A read returning 0 with done 0 is end of stream.
    virtual int read(uint8_t* p, size_t n, size_t& done) = 0;
    virtual int write(const uint8_t* p, size_t n, size_t& done) = 0;
    // Wait until a read (in) or a write could move bytes, the stream is
    // closed, or d passes (ETIMEDOUT). May return 0 early.
    virtual int wait(bool in, deadline d) = 0;
    // Wake every blocked call; reads then see end of stream, writes fail.
    virtual void close() = 0;
};

// Transport over s, starting its receiver thread. Failure of the stream
// (end of stream, reset, a malformed or unmatched frame, a send that cannot
// finish by its deadline) fails the session once through failed(): ESHUTDOWN
// after shutdown(), ECONNRESET at end of stream, EPROTO for a bad frame,
// else the stream's errno. Nothing is resent.
std::unique_ptr<transport> stream_transport(std::unique_ptr<stream> s);

}

#endif
