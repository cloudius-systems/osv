/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "session.hh"

#include <cerrno>
#include <mutex>
#include <new>

namespace p9 {

namespace {

uint32_t get_u32(const bytes& b, size_t at)
{
    return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8
        | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}

// The reply bound check_reply() needs: Twalk nwname, Tread/Twrite count.
int limit_of(uint8_t type, const bytes& b, uint32_t& limit)
{
    limit = 0;
    switch (type) {
    case Twalk: // fid[4] newfid[4] nwname[2]
        if (b.size() < 10) {
            return EINVAL;
        }
        limit = uint32_t(b[8] | b[9] << 8);
        return 0;
    case Tread: // fid[4] offset[8] count[4]
    case Twrite:
        if (b.size() < 16) {
            return EINVAL;
        }
        limit = get_u32(b, 12);
        return 0;
    default:
        return 0;
    }
}

void put_u16(bytes& b, uint16_t v)
{
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

void put_u32(bytes& b, uint32_t v)
{
    put_u16(b, uint16_t(v));
    put_u16(b, uint16_t(v >> 16));
}

int put_str(bytes& b, const std::string& str)
{
    if (str.size() > 0xffff) {
        return ENAMETOOLONG;
    }
    put_u16(b, uint16_t(str.size()));
    b.insert(b.end(), str.begin(), str.end());
    return 0;
}

}

session::session(std::unique_ptr<transport> t, size_t ceiling)
    : _t(std::move(t)), _table(ceiling)
{
}

session::~session()
{
    shutdown(op_deadline());
    // Destroy the transport while the session is intact: it joins its
    // threads, and any request it never retired stays alive through the
    // transport's own share, never freed here.
    _t.reset();
}

// ETIMEDOUT if d has passed, else wait for a wake or d and return 0, so
// callers recheck their condition once more after the timer fires.
int session::wait(deadline d)
{
    if (osv::clock::uptime::now() >= d) {
        return ETIMEDOUT;
    }
    _cv.wait(&_mtx, d);
    return 0;
}

// Apply completions recorded by received()/retired(), in caller context:
// decode arrivals in order, then buffer retirements, then free requests
// whose table slot is fully retired. Called with _mtx held.
void session::drain()
{
    if (_bad) {
        _table.fail(EPROTO);
    }
    if (_failed) {
        _table.fail(_failed);
    }
    for (unsigned i = 0; i < _narrived; i++) {
        unsigned id = _arrived[i];
        request& r = *_req[id];
        frame f;
        int rc = decode(r.rx.data(), r.rx_len, _msize ? _msize : _offered, f);
        if (id == version_id) {
            uint32_t m = 0;
            if (!rc) {
                rc = check_version(f, _offered, m);
            }
            _version_err = rc;
            if (rc) {
                _table.fail(rc);
            } else {
                _msize = m;
            }
        } else if (rc) {
            _table.fail(rc);
        } else {
            _table.received(std::move(f));
        }
    }
    _narrived = 0;
    if (_table.dead() && _version_err == EINPROGRESS) {
        _version_err = _table.dead();
    }
    for (unsigned id = 0; id <= version_id; id++) {
        std::shared_ptr<request>& r = _req[id];
        if (!r) {
            continue;
        }
        bool busy;
        if (id == version_id) {
            busy = !r->gone || _version_err == EINPROGRESS;
        } else {
            if (r->gone && !r->refunded) {
                _table.retired(id);
                r->refunded = true;
            }
            // Fid pins last until the server is done with the request (its
            // tag retired, by reply, Rflush or session failure); buffer
            // retirement concerns memory only. Tags retire only inside
            // drain() or by fail(), and are reissued only by admit(), which
            // always follows a drain(); so this test is exact.
            if (!_table.tags().held(r->tag)) {
                r->tag_done = true;
                for (auto& p : r->pin) {
                    unpin(p);
                    p = nofid;
                }
            }
            busy = _table.busy(id);
        }
        if (!busy) {
            r.reset();
            _cv.wake_all(); // a slot, its charge or fids may now be free
        }
    }
}

void session::received(request& r, size_t n)
{
    WITH_LOCK(_mtx) {
        if (r.id > version_id || _req[r.id].get() != &r || r.arrived
            || r.gone || n > r.rx.size()) {
            _bad = true;
        } else {
            r.arrived = true;
            r.rx_len = n;
            _arrived[_narrived++] = r.id;
        }
        _cv.wake_all();
    }
}

void session::retired(request& r)
{
    WITH_LOCK(_mtx) {
        if (r.id > version_id || _req[r.id].get() != &r || r.gone) {
            _bad = true;
        } else {
            r.gone = true;
        }
        _cv.wake_all();
    }
}

// Caller holds pins on pin0/pin1 (nofid for none); on return they belong
// to the request if one was admitted, else they are dropped here.
int session::call(uint8_t type, const bytes& body, frame& reply, deadline d,
                  uint32_t pin0, uint32_t pin1, outcome* out)
{
    outcome dummy;
    if (!out) {
        out = &dummy;
    }
    *out = outcome{};
    std::shared_ptr<request> r;
    int rc = 0;
    uint32_t limit;
    try {
        r = std::make_shared<request>();
    } catch (const std::bad_alloc&) {
        rc = ENOMEM;
    }
    if (!rc) {
        rc = limit_of(type, body, limit);
    }
    std::unique_lock<mutex> lk(_mtx);
    const uint32_t msize = _msize;
    if (!rc && !msize) {
        rc = _table.dead() ? _table.dead() : ENOTCONN;
    }
    const unsigned gen = _abort_gen;
    if (!rc && body.size() > msize - 7) {
        rc = EMSGSIZE;
    }
    if (rc) {
        unpin(pin0);
        unpin(pin1);
        return rc;
    }
    r->owner = this;
    r->pin[0] = pin0;
    r->pin[1] = pin1;
    // Admission charges both buffers before anything is allocated, so
    // callers waiting for a slot hold no memory.
    unsigned id;
    uint16_t tag;
    for (;;) {
        drain();
        rc = _table.admit(type, limit, 7 + body.size() + msize, id, tag);
        // Wait for a slot (EAGAIN) or for charged bytes to be refunded
        // (ENOMEM), never past d.
        if ((rc != EAGAIN && rc != ENOMEM)
            || (rc == ENOMEM && _table.charged() == 0)) {
            break;
        }
        if (_abort_gen != gen) {
            rc = _abort_err; // nothing was sent
            break;
        }
        if (wait(d)) {
            rc = ETIMEDOUT; // nothing was sent
            break;
        }
    }
    if (rc) {
        unpin(pin0);
        unpin(pin1);
        return rc;
    }
    // Allocate buffers and encode in caller context without the lock. The
    // slot is ours; nothing else touches _req[id] until it is published.
    // (OSv malloc waits rather than failing; bad_alloc is possible only in
    // hosted builds, and fails the session below.)
    lk.unlock();
    r->id = id;
    r->tag = tag;
    rc = encode(frame{type, tag, body}, msize, r->tx);
    try {
        r->rx.resize(msize);
    } catch (const std::bad_alloc&) {
        rc = ENOMEM;
    }
    lk.lock();
    _req[id] = r;
    out->sent = true; // or failed in a way that fails the session
    if (!rc) {
        rc = _table.dead();
    }
    if (!rc) {
        lk.unlock();
        rc = _t->submit(r, d);
        lk.lock();
    }
    if (rc) {
        // Not sent (or not known to be), so nothing will be retired by the
        // transport. A stream with a half-sent or missing frame cannot be
        // resynchronized: the session fails.
        r->gone = true;
        _table.fail(rc);
    }
    // Collect the result. If d passes (or abort()) first, cancel with a
    // Tflush; a caller that sent one also waits until it resolves, so every
    // flush has someone enforcing its deadline.
    int result = EINPROGRESS;
    int cancel = 0; // errno once canceling
    bool flush_sent = false;
    deadline fd{};
    for (;;) {
        drain();
        if (result == EINPROGRESS) {
            int err;
            int c = _table.collect(id, err, reply);
            if (c != EINPROGRESS) {
                result = c ? c : err;
                out->replied = c == 0 && reply.type != 0;
            }
        }
        if (result != EINPROGRESS && (!flush_sent || r->tag_done)) {
            drain(); // collect() may have freed the slot
            return result;
        }
        auto now = osv::clock::uptime::now();
        if (result == EINPROGRESS && !cancel
            && (_abort_gen != gen || now >= d)) {
            cancel = _abort_gen != gen ? _abort_err : ETIMEDOUT;
            fd = now + flush_timeout; // bounds waiting for the flush slot
        }
        if (cancel && !flush_sent && result == EINPROGRESS) {
            // Serialized: EAGAIN while another flush holds the slot.
            int f = flush(lk, id, cancel);
            if (f != EAGAIN) {
                flush_sent = f == 0;
                out->flushed = flush_sent ? cancel : 0;
                fd = osv::clock::uptime::now() + flush_timeout;
                continue;
            }
        }
        if (wait(cancel ? fd : d) && cancel) {
            _table.fail(ETIMEDOUT); // no Rflush within flush_timeout
        }
    }
}

// Cancel id with err: reserve the flush slot in the table, then build and
// submit Tflush(oldtag) without the lock. 0 once sent (or once the attempt
// failed the session); EAGAIN if the slot is busy; EALREADY if id already
// completed or the session is dead. Called and returns with lk held.
int session::flush(std::unique_lock<mutex>& lk, unsigned id, int err)
{
    uint16_t ftag;
    uint16_t oldtag = _req[id]->tag;
    int rc = _table.cancel(id, err, ftag);
    if (rc) {
        return rc == EAGAIN ? EAGAIN : EALREADY;
    }
    const uint32_t msize = _msize;
    lk.unlock();
    std::shared_ptr<request> f;
    try {
        f = std::make_shared<request>();
        f->owner = this;
        f->id = request_table::flush_id;
        f->tag = ftag;
        rc = encode(frame{Tflush, ftag, {uint8_t(oldtag),
                                         uint8_t(oldtag >> 8)}},
                    msize, f->tx);
        f->rx.resize(7); // Rflush is header only; anything longer is EPROTO
    } catch (const std::bad_alloc&) {
        rc = ENOMEM;
    }
    lk.lock();
    if (!f) {
        _table.fail(ENOMEM);
        _table.retired(request_table::flush_id);
        return 0;
    }
    _req[request_table::flush_id] = f;
    if (!rc) {
        rc = _table.dead();
    }
    if (!rc) {
        lk.unlock();
        rc = _t->submit(f, osv::clock::uptime::now() + flush_timeout);
        lk.lock();
    }
    if (rc) {
        f->gone = true;
        _table.fail(rc);
    }
    return 0;
}

void session::abort(int err)
{
    WITH_LOCK(_mtx) {
        if (err) {
            _abort_err = err;
            ++_abort_gen;
            _cv.wake_all();
        }
    }
}

unsigned session::completions()
{
    WITH_LOCK(_mtx) {
        drain();
        return _table.completions();
    }
}

void session::failed(int err)
{
    WITH_LOCK(_mtx) {
        if (!_failed) {
            _failed = err ? err : EIO;
        }
        _cv.wake_all();
    }
}

int session::shutdown(deadline d)
{
    std::unique_lock<mutex> lk(_mtx);
    if (!_failed) {
        _failed = ESHUTDOWN;
    }
    drain(); // fails the table: every waiter will see ESHUTDOWN
    _cv.wake_all();
    lk.unlock();
    int rc = _t->shutdown(d);
    lk.lock();
    drain();
    for (auto& r : _req) {
        if (r && !r->gone) {
            return EBUSY;
        }
    }
    return rc ? EBUSY : 0;
}

int session::dead()
{
    WITH_LOCK(_mtx) {
        drain();
        return _table.dead();
    }
}

size_t session::charged()
{
    WITH_LOCK(_mtx) {
        drain();
        return _table.charged();
    }
}

unsigned session::busy()
{
    WITH_LOCK(_mtx) {
        drain();
        unsigned n = 0;
        for (unsigned id = 0; id <= data_tags; id++) {
            n += _table.busy(id);
        }
        return n;
    }
}

// Called with _mtx held, after drain().
int session::pin_live(uint32_t fid)
{
    if (_fids.get(fid) != fid_table::state::live || _closing[fid]) {
        return EBADF;
    }
    ++_pins[fid];
    return 0;
}

void session::unpin(uint32_t fid)
{
    if (fid == nofid) {
        return;
    }
    if (--_pins[fid] == 0 && _doomed[fid]) {
        _doomed[fid] = false;
        _closing[fid] = false;
        _fids.release(fid);
    }
}

void session::drop_fid(uint32_t fid)
{
    _doomed[fid] = true;
    unpin(fid);
}

int session::rpc(uint8_t type, const bytes& body, frame& reply, deadline d)
{
    if ((type != Tlopen && type != Tread && type != Twrite)
        || body.size() < 4) {
        return EINVAL;
    }
    uint32_t fid = get_u32(body, 0);
    WITH_LOCK(_mtx) {
        drain();
        if (int rc = pin_live(fid)) {
            return rc;
        }
    }
    return call(type, body, reply, d, fid, nofid);
}

int session::attach(const std::string& uname, const std::string& aname,
                    uint32_t n_uname, uint32_t& fid, bytes& qid, deadline d)
{
    uint32_t f;
    WITH_LOCK(_mtx) {
        drain();
        if (int rc = _fids.reserve(f)) {
            return rc;
        }
        _pins[f] = 2; // ours and the request's
    }
    bytes body;
    put_u32(body, f);
    put_u32(body, nofid);
    int rc = put_str(body, uname);
    if (!rc) {
        rc = put_str(body, aname);
    }
    put_u32(body, n_uname);
    frame reply;
    if (rc) {
        WITH_LOCK(_mtx) {
            unpin(f);
            drop_fid(f);
        }
    } else {
        outcome o;
        rc = call(Tattach, body, reply, d, f, nofid, &o);
        rc = created(f, rc, o);
    }
    if (!rc) {
        fid = f;
        qid = std::move(reply.body);
    }
    return rc;
}

int session::walk(uint32_t fid, const std::vector<std::string>& names,
                  uint32_t& newfid, bytes& qids, deadline d)
{
    if (names.size() > 16) { // MAXWELEM, walk(5)
        return E2BIG;
    }
    uint32_t f;
    WITH_LOCK(_mtx) {
        drain();
        if (int rc = pin_live(fid)) {
            return rc;
        }
        if (int rc = _fids.reserve(f)) {
            unpin(fid);
            return rc;
        }
        _pins[f] = 2;
    }
    bytes body;
    put_u32(body, fid);
    put_u32(body, f);
    put_u16(body, uint16_t(names.size()));
    int rc = 0;
    for (auto& n : names) {
        if (!rc) {
            rc = put_str(body, n);
        }
    }
    frame reply;
    if (rc) {
        WITH_LOCK(_mtx) {
            unpin(fid);
            unpin(f);
            drop_fid(f);
        }
        return rc;
    }
    outcome o;
    rc = call(Twalk, body, reply, d, fid, f, &o);
    // Rwalk nwqid[2] then qids; check_reply bounded nwqid by nwname. A
    // partial walk did not create newfid (walk(5)).
    if (!rc && uint16_t(reply.body[0] | reply.body[1] << 8) != names.size()) {
        o.replied = false;
        rc = ENOENT;
    }
    rc = created(f, rc, o);
    if (!rc) {
        newfid = f;
        qids.assign(reply.body.begin() + 2, reply.body.end());
    }
    return rc;
}

int session::clunk(uint32_t fid, deadline d)
{
    WITH_LOCK(_mtx) {
        drain();
        if (_fids.get(fid) != fid_table::state::live || _closing[fid]) {
            return EBADF;
        }
        if (_pins[fid]) {
            return EBUSY;
        }
        _closing[fid] = true;
        _pins[fid] = 2; // ours and the request's
    }
    bytes body;
    put_u32(body, fid);
    frame reply;
    outcome o;
    int rc = call(Tclunk, body, reply, d, fid, nofid, &o);
    WITH_LOCK(_mtx) {
        if (o.sent && o.flushed && !o.replied) {
            // Canceled before any reply: the server may or may not have
            // clunked the fid, and it cannot be asked again. Retire the
            // session rather than guess.
            _table.fail(EIO);
        }
        if (o.sent || _table.dead()) {
            drop_fid(fid); // clunk(5): gone whatever the reply
        } else {
            _closing[fid] = false; // never sent: still live on the server
            unpin(fid);
        }
    }
    return rc;
}

// Resolve a reserved fid f after the Tattach/Twalk that names it. On
// success it becomes live. If the server created it but the caller had
// already canceled (reply won the race with Tflush), it is clunked and the
// cancellation error returned; if that clunk is uncertain, clunk() fails
// the session. Otherwise f is released.
int session::created(uint32_t f, int rc, const outcome& o)
{
    bool orphan = o.flushed && o.replied && rc == 0;
    WITH_LOCK(_mtx) {
        if (rc) {
            drop_fid(f);
            return rc;
        }
        _fids.make_live(f);
        unpin(f);
    }
    if (orphan) {
        // ponytail: compensating clunk waits a fresh op deadline; the
        // caller's deadline already passed.
        clunk(f, op_deadline());
        return o.flushed;
    }
    return 0;
}

int session::negotiate(uint32_t msize, deadline d)
{
    if (msize < min_msize || msize > 65536) {
        return EINVAL;
    }
    auto r = std::make_shared<request>();
    r->owner = this;
    r->id = version_id;
    r->tag = notag;
    bytes body;
    put_u32(body, msize);
    put_str(body, "9P2000.L");
    int rc = encode(frame{Tversion, notag, body}, msize, r->tx);
    if (rc) {
        return rc;
    }
    r->rx.resize(msize);
    std::unique_lock<mutex> lk(_mtx);
    if (_offered || _table.dead()) {
        return _table.dead() ? _table.dead() : EALREADY;
    }
    _offered = msize;
    _req[version_id] = r;
    lk.unlock();
    rc = _t->submit(r, d);
    lk.lock();
    if (rc) {
        r->gone = true;
        _table.fail(rc);
    }
    for (;;) {
        drain();
        if (_version_err != EINPROGRESS) {
            return _version_err;
        }
        if (wait(d)) {
            _table.fail(ETIMEDOUT);
        }
    }
}

uint32_t session::msize()
{
    WITH_LOCK(_mtx) {
        drain();
        return _msize;
    }
}

unsigned session::fids()
{
    WITH_LOCK(_mtx) {
        drain();
        return _fids.used();
    }
}

}
