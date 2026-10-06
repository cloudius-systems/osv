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
#include <bitset>
#include <memory>
#include <string>
#include <vector>

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
    uint32_t pin[2] = {nofid, nofid}; // fids held until the tag retires
    bool tag_done = false; // server will send nothing more for it
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

    // Send Tversion(msize, "9P2000.L") on NOTAG and wait until d. 0 and
    // msize() set; EINVAL for msize outside [min_msize, 65536]; EALREADY
    // once negotiated or in progress. Any other failure (EPROTONOSUPPORT
    // for another dialect, EPROTO, ETIMEDOUT) fails the session.
    int negotiate(uint32_t msize, deadline d);
    uint32_t msize();

    // Send Tlopen, Tread or Twrite (EINVAL for other types) and wait until
    // d for the reply. The fid that starts the body must be live (EBADF
    // otherwise, or while it is being clunked) and stays pinned until the
    // server is done with the request. Returns 0 with the reply, the mapped errno of
    // an Rlerror (reply holds it), ENOTCONN before negotiate(), EMSGSIZE if
    // the frame exceeds msize, ETIMEDOUT if no data slot frees before d
    // (nothing sent), or the session error once failed.
    //
    // Cancellation: once d passes (or abort() is called) with the request
    // outstanding, Tflush(oldtag) goes out on the reserved flush tag and the
    // caller waits a further flush_timeout for Rflush; without it the
    // session fails. A reply that wins the race is honored once (returned
    // here); otherwise the result is ETIMEDOUT (or the abort errno).
    int rpc(uint8_t type, const bytes& body, frame& reply, deadline d);

    // Fid helpers. Each fid is released exactly once, and only after every
    // request that names it has retired, so it is never reused while the
    // server may still act on it. ENFILE when max_fids are in use.
    // Tattach(fid, NOFID, uname, aname, n_uname) on a new fid; 0 with the
    // live fid and the 13-byte qid. attach and walk cancel like rpc(); if
    // the server creates the fid anyway (reply before Rflush), the fid is
    // clunked and the cancellation error returned.
    int attach(const std::string& uname, const std::string& aname,
               uint32_t n_uname, uint32_t& fid, bytes& qid, deadline d);
    // Twalk(fid, newfid, names) on a new fid; 0 with the live newfid and
    // its qids only if every name was walked, ENOENT for a partial walk.
    int walk(uint32_t fid, const std::vector<std::string>& names,
             uint32_t& newfid, bytes& qids, deadline d);
    // Tclunk(fid): once sent, the fid is released whatever the reply
    // (clunk(5)); if nothing was sent (no slot before d) it stays live. A
    // clunk canceled without a reply leaves the server's fid state unknown,
    // which fails the session. EBADF if fid is not live or already being
    // clunked, EBUSY while a request pins it.
    int clunk(uint32_t fid, deadline d);

    // Cancel every request started before this call with err (nonzero),
    // as if its deadline had passed; one still waiting for a slot returns
    // err with nothing sent. Later requests are unaffected.
    void abort(int err);

    // Transport completions; see struct request.
    void received(request& r, size_t n);
    void retired(request& r);

    // Observers; they first apply pending completions.
    int dead();
    size_t charged();
    unsigned busy(); // request slots not yet fully retired
    unsigned fids(); // fids reserved or live
    unsigned completions(); // data requests completed, ever

private:
    static constexpr unsigned version_id = data_tags + 1;
    int wait(deadline d);
    void drain();
    struct outcome {
        bool sent = false;    // submitted, or failed the session trying
        bool replied = false; // the server's reply was returned
        int flushed = 0;      // errno a Tflush was sent for, else 0
    };
    // rpc() with fids already pinned by the caller; unpinned when the
    // request's tag retires (or at once if nothing was admitted).
    int call(uint8_t type, const bytes& body, frame& reply, deadline d,
             uint32_t pin0, uint32_t pin1, outcome* out = nullptr);
    int flush(std::unique_lock<mutex>& lk, unsigned id, int err);
    int created(uint32_t fid, int rc, const outcome& o);
    int pin_live(uint32_t fid);
    void unpin(uint32_t fid);
    void drop_fid(uint32_t fid); // release now or at the last unpin

    std::unique_ptr<transport> _t;
    mutex _mtx;
    condvar _cv;
    request_table _table;
    // Replies in arrival order, decoded later in caller context so the
    // table sees them in the order the transport delivered them.
    // Data slots, the flush slot, then the Tversion slot (not in _table).
    std::array<std::shared_ptr<request>, version_id + 1> _req;
    std::array<unsigned, version_id + 1> _arrived;
    unsigned _narrived = 0;
    bool _bad = false; // transport broke the completion contract
    uint32_t _msize = 0; // 0 until negotiated
    uint32_t _offered = 0;
    int _version_err = EINPROGRESS;
    unsigned _abort_gen = 0; // bumped by abort()
    int _abort_err = 0;
    fid_table _fids;
    std::array<uint32_t, max_fids> _pins{}; // helpers and requests holding a fid
    std::bitset<max_fids> _closing; // clunk in progress: no new pins
    std::bitset<max_fids> _doomed;  // release at the last unpin
};

}

#endif
