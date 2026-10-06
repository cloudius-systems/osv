/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_FS_9P_SESSION_HH
#define OSV_FS_9P_SESSION_HH

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <osv/clock.hh>
#include <osv/condvar.h>
#include <osv/mutex.h>

#include "codec.hh"
#include "session_state.hh"

// Concurrency layer of a 9P2000.L session: the request_table and fid_table
// of session_state.hh under one OSv mutex, synchronous RPCs with absolute
// deadlines, and the internal transport contract shared by TCP and virtio.
namespace p9 {

using deadline = osv::clock::uptime::time_point;
constexpr std::chrono::seconds op_timeout{30};
constexpr std::chrono::seconds flush_timeout{5};

inline deadline op_deadline()
{
    return osv::clock::uptime::now() + op_timeout;
}

class session;

// One outstanding T-message. Owned by the session and, from a successful
// submit() until retired() returns, shared with the transport; never by the
// caller's stack. The transport reads tx, writes at most rx.size() reply
// bytes into rx, then calls owner->received(*this, n) at most once and
// owner->retired(*this) exactly once, and touches neither buffer after
// retired(). Both calls take the session mutex, record and wake, and never
// allocate or free: safe from a completion thread, not from interrupt
// context. Decoding happens later in the waiting caller.
struct request {
    session* owner = nullptr;
    unsigned id = 0;
    uint16_t tag = notag;
    bytes tx;
    bytes rx;
    // Private to the session, under its mutex.
    size_t rx_len = 0;
    bool arrived = false;
    bool gone = false;    // retired() called
    bool refunded = false; // retirement applied to the table
};

class transport {
public:
    virtual ~transport() {}
    // Take a share of r and send r->tx. 0, or an errno meaning the
    // transport kept nothing and will call nothing for r; the session then
    // fails, since a partial send cannot be undone.
    virtual int submit(std::shared_ptr<request> r, deadline d) = 0;
    // Stop: retire every request held, join internal threads. 0, or an
    // errno if some buffer may still be referenced at d.
    virtual int shutdown(deadline d) = 0;
};

class session {
public:
    // ceiling bounds the bytes charged by outstanding requests.
    explicit session(std::unique_ptr<transport> t,
                     size_t ceiling = alloc_ceiling);
    ~session();
    session(const session&) = delete;
    session& operator=(const session&) = delete;

    // Send T-message type with body and wait until d for its reply.
    // Returns 0 with the reply, the mapped errno of an Rlerror (reply
    // holds it), EMSGSIZE if the frame exceeds msize, ETIMEDOUT if no data
    // slot frees before d (nothing sent), or the session error once failed.
    int rpc(uint8_t type, const bytes& body, frame& reply, deadline d);

    // Transport completions; see struct request.
    void received(request& r, size_t n);
    void retired(request& r);

    // Observers; they first apply pending completions.
    int dead();
    size_t charged();
    unsigned busy(); // request slots not yet fully retired

private:
    int wait(deadline d);
    void drain();

    std::unique_ptr<transport> _t;
    mutex _mtx;
    condvar _cv;
    request_table _table;
    // Replies in arrival order, decoded later in caller context so the
    // table sees them in the order the transport delivered them.
    std::array<std::shared_ptr<request>, data_tags + 1> _req;
    std::array<unsigned, data_tags + 1> _arrived;
    unsigned _narrived = 0;
    bool _bad = false; // transport broke the completion contract
    uint32_t _msize = min_msize;
};

}

#endif
