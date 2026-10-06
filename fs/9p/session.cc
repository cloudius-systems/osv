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

}

session::session(std::unique_ptr<transport> t, size_t ceiling)
    : _t(std::move(t)), _table(ceiling)
{
}

session::~session()
{
    _t->shutdown(op_deadline());
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
    for (unsigned i = 0; i < _narrived; i++) {
        request& r = *_req[_arrived[i]];
        frame f;
        int rc = decode(r.rx.data(), r.rx_len, _msize, f);
        if (rc) {
            _table.fail(rc);
        } else {
            _table.received(std::move(f));
        }
    }
    _narrived = 0;
    for (unsigned id = 0; id <= data_tags; id++) {
        std::shared_ptr<request>& r = _req[id];
        if (r && r->gone && !r->refunded) {
            _table.retired(id);
            r->refunded = true;
        }
        if (r && !_table.busy(id)) {
            r.reset();
        }
    }
}

void session::received(request& r, size_t n)
{
    WITH_LOCK(_mtx) {
        if (r.id > data_tags || _req[r.id].get() != &r || r.arrived
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
        if (r.id > data_tags || _req[r.id].get() != &r || r.gone) {
            _bad = true;
        } else {
            r.gone = true;
        }
        _cv.wake_all();
    }
}

int session::rpc(uint8_t type, const bytes& body, frame& reply, deadline d)
{
    uint32_t limit;
    if (int rc = limit_of(type, body, limit)) {
        return rc;
    }
    std::shared_ptr<request> r;
    try {
        r = std::make_shared<request>();
    } catch (const std::bad_alloc&) {
        return ENOMEM;
    }
    r->owner = this;
    std::unique_lock<mutex> lk(_mtx);
    const uint32_t msize = _msize;
    if (body.size() > msize - 7) {
        return EMSGSIZE;
    }
    // Admission charges both buffers before anything is allocated, so
    // callers waiting for a slot hold no memory.
    unsigned id;
    uint16_t tag;
    int rc;
    for (;;) {
        drain();
        rc = _table.admit(type, limit, 7 + body.size() + msize, id, tag);
        // Wait for a slot (EAGAIN) or for charged bytes to be refunded
        // (ENOMEM), never past d.
        if ((rc != EAGAIN && rc != ENOMEM)
            || (rc == ENOMEM && _table.charged() == 0)) {
            break;
        }
        if (wait(d)) {
            return ETIMEDOUT; // nothing was sent
        }
    }
    if (rc) {
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
    for (;;) {
        drain();
        int err;
        int c = _table.collect(id, err, reply);
        if (c != EINPROGRESS) {
            drain();
            return c ? c : err;
        }
        if (wait(d)) {
            // ponytail: no Tflush yet, so a timed-out request fails the
            // session; cancellation replaces this.
            _table.fail(ETIMEDOUT);
        }
    }
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

}
