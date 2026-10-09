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
// Flushes are serialized on one reserved tag. A canceling caller may queue
// behind every other data request's flush, each answered within
// flush_timeout, so its wait for the slot gets a bound of its own.
constexpr std::chrono::seconds flush_slot_timeout = flush_timeout * data_tags;

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
// context. Decoding happens later in the waiting caller; a reply whose
// tag differs from r.tag fails the session with EPROTO.
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
    // errno if some buffer may still be referenced at d. The destructor
    // must join any thread that can call into the session; it may keep its
    // shares of requests it could not retire.
    virtual int shutdown(deadline d) = 0;
};

class session {
public:
    // ceiling bounds the bytes charged by outstanding requests: each data
    // request's tx (7 + body) and rx (msize) buffers, from admission until
    // the transport retires them. Not charged, and bounded separately:
    //   decoded reply copies waiting in uncollected slots, at most
    //     data_tags x (msize - 7) (about 2 MiB at msize 65536); a reply
    //     becomes the caller's at collect;
    //   the Tversion request (rx of msize), once per session;
    //   the Tflush request (9 + 7 bytes), so cancellation never waits for
    //     budget;
    //   a returning caller's retired request object, freed as rpc()
    //     returns (one per calling thread).
    // So the worst case is about ceiling + data_tags x msize.
    // slot_bound replaces flush_slot_timeout (tests use a short one).
    explicit session(std::unique_ptr<transport> t,
                     size_t ceiling = alloc_ceiling,
                     osv::clock::uptime::duration slot_bound
                         = flush_slot_timeout);
    // shutdown(op_deadline()), then destroy the transport. No caller may
    // still be inside the session.
    ~session();
    session(const session&) = delete;
    session& operator=(const session&) = delete;

    // Send Tversion(msize, "9P2000.L") on NOTAG and wait until d. 0 and
    // msize() set; EINVAL for msize outside [min_msize, 65536]; EALREADY
    // once negotiated or in progress. Any other failure (EPROTONOSUPPORT
    // for another dialect, EPROTO, ETIMEDOUT) fails the session.
    int negotiate(uint32_t msize, deadline d);
    uint32_t msize();

    // Send Tlopen, Tread, Twrite, Tgetattr, Treaddir, Tlcreate, Tmkdir,
    // Tunlinkat, Trenameat, Trename, Tstatfs or Tsetattr (EINVAL for other
    // types) and wait until d for the reply. The fid that starts the body,
    // and Trenameat's newdirfid or Trename's dfid, must be live (EBADF
    // otherwise, or while it is being clunked) and stay pinned until the
    // server is done with the request. Returns 0 with the reply, the mapped errno of
    // an Rlerror (reply holds it), ENOTCONN before negotiate(), EMSGSIZE if
    // the frame exceeds msize, ETIMEDOUT if no data slot frees before d
    // (nothing sent), or the session error once failed.
    //
    // Cancellation: once d passes (or abort() is called) with the request
    // outstanding, Tflush(oldtag) goes out on the reserved flush tag and the
    // caller waits a further flush_timeout for Rflush; without it the
    // session fails. Flushes are serialized: the wait for the flush slot is
    // bounded separately by flush_slot_timeout, and its expiry also fails
    // the session (a slot held that long means the server or transport is
    // not retiring flushes). A reply that wins the race is honored once (returned
    // here); otherwise the result is ETIMEDOUT (or the abort errno).
    //
    // Worst case, a caller returns by d + flush_slot_timeout + 2 *
    // flush_timeout (d + 170 s): the Tflush send and the Rflush wait are
    // each bounded by flush_timeout. attach() and walk() can add a
    // compensating clunk with its own op_deadline() and the same
    // cancellation bounds, so they return by d + 370 s. Every wait is
    // bounded; these are expiry limits, not queue guarantees (the flush
    // slot is not FIFO).
    int rpc(uint8_t type, const bytes& body, frame& reply, deadline d);

    // Fid helpers. Each fid is released exactly once, and only after every
    // request that names it has retired, so it is never reused while the
    // server may still act on it. ENFILE when max_fids are in use.
    // Tattach(fid, NOFID, uname, aname, n_uname) on a new fid; 0 with the
    // live fid and the 13-byte qid. attach and walk cancel like rpc(); if
    // the server creates the fid anyway (reply before Rflush), the fid is
    // clunked and the cancellation error returned. The fid is never
    // visible as live to other callers; if that clunk cannot be sent (no
    // slot before a fresh op_deadline(), or abort()) the session fails.
    int attach(const std::string& uname, const std::string& aname,
               uint32_t n_uname, uint32_t& fid, bytes& qid, deadline d);
    // Twalk(fid, newfid, names) on a new fid; 0 with the live newfid and
    // its qids only if every name was walked, ENOENT for a partial walk.
    // After a partial Rwalk (0 < nwqid < nwname) newfid is clunked like a
    // canceled one before ENOENT is returned: walk(5) says it was not
    // created, but diod keeps it. Any reply to that Tclunk releases it.
    // A partial Rwalk that wins a race with Tflush still returns ENOENT,
    // not the cancel errno: the reply is honored, as rpc() describes.
    int walk(uint32_t fid, const std::vector<std::string>& names,
             uint32_t& newfid, bytes& qids, deadline d);
    // Tclunk(fid): once sent, the fid is released whatever the reply
    // (clunk(5)); if nothing was sent (no slot before d) it stays live and
    // ETIMEDOUT (or the abort errno) is returned. A clunk canceled without
    // a reply leaves the server's fid state unknown, which fails the
    // session with EIO and returns that session error (dead() == EIO), not
    // the cancel errno. A server Rlerror(EIO) also returns EIO with the
    // session alive; dead() tells them apart. Whatever the return value,
    // if dead() is nonzero afterwards the fid is gone. EBADF if fid is not
    // live or already being clunked, EBUSY while a request pins it.
    int clunk(uint32_t fid, deadline d);
    // Tremove(fid): remove the object, and release the fid exactly as
    // clunk() does whatever the reply (remove(5)); the server's errno is
    // returned.
    int remove(uint32_t fid, deadline d);
    // clunk() or remove() for a fid whose caller keeps no handle to it (a
    // vnode being freed, an unwind, a file being closed): if nothing can
    // be sent before d, the fid cannot be left live with no owner, so the
    // session fails (EIO) and the fid is gone, as for an orphan. EBADF and
    // EBUSY as for clunk().
    int discard(uint32_t fid, deadline d, uint8_t type = Tclunk);

    // Cancel every request started before this call with err (nonzero),
    // as if its deadline had passed; one still waiting for a slot returns
    // err with nothing sent. Later requests are unaffected.
    void abort(int err);

    // Transport completions; see struct request.
    void received(request& r, size_t n);
    void retired(request& r);
    // The transport is broken (EOF, reset, device error): every waiter
    // wakes once with err (nonzero) and nothing is resent. The transport
    // still retires each request it holds. Same context rules as above.
    void failed(int err);

    // Fail the session with ESHUTDOWN, wake every waiter, and stop the
    // transport, waiting until d. Buffers are released only after the
    // transport has retired them: 0 once all are, else EBUSY (requests
    // remain owned and shutdown() may be called again). Idempotent.
    int shutdown(deadline d);

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
    int created(uint32_t fid, int rc, const outcome& o, bool partial = false);
    int release(uint32_t fid, deadline d, uint8_t type, bool must = false);
    int send_clunk(uint32_t fid, deadline d, bool must,
                   uint8_t type = Tclunk);
    int pin_live(uint32_t fid);
    void unpin(uint32_t fid);
    void drop_fid(uint32_t fid); // release now or at the last unpin

    std::unique_ptr<transport> _t;
    const osv::clock::uptime::duration _slot_bound;
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
    int _failed = 0;   // errno from failed()
    uint32_t _msize = 0; // 0 until negotiated
    uint32_t _offered = 0;
    bool _version_done = false; // Rversion applied or session failed
    int _version_err = 0;
    unsigned _abort_gen = 0; // bumped by abort()
    int _abort_err = 0;
    fid_table _fids;
    std::array<uint32_t, max_fids> _pins{}; // helpers and requests holding a fid
    std::bitset<max_fids> _closing; // clunk in progress: no new pins
    std::bitset<max_fids> _doomed;  // release at the last unpin
};

}

#endif
