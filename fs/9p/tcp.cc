/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Stream framing for the 9P session (fs/9p/session.hh transport contract).
// Every frame is size[4] type[1] tag[2] body, little endian, with size
// counting the header (Plan 9 intro(5)); Rflush means the server will send
// nothing more for the flushed tag (flush(5)). No implementation imported.

#include "tcp.hh"

#include <algorithm>
#include <cerrno>
#include <thread>
#include <vector>

namespace p9 {

namespace {

uint32_t le32(const uint8_t* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16
        | uint32_t(p[3]) << 24;
}

uint16_t le16(const uint8_t* p)
{
    return uint16_t(p[0] | p[1] << 8);
}

class stream_tp : public transport {
public:
    explicit stream_tp(std::unique_ptr<stream> s) : _s(std::move(s))
    {
        _rx = std::thread([this] { receive(); });
    }
    ~stream_tp() override
    {
        shutdown(deadline::max());
        if (_rx.joinable()) {
            _rx.join();
        }
        // _s (the socket) is released only now, after the join.
    }
    int submit(std::shared_ptr<request> r, deadline d) override;
    int shutdown(deadline d) override;

private:
    // A request is matched by its tag once its whole frame is written.
    struct entry {
        std::shared_ptr<request> r;
        bool sent = false;
    };
    void receive();
    int read_full(uint8_t* p, size_t n);
    int sleep(deadline d); // _cv wait until d (none for deadline::max())
    void fail(int err);    // with _mtx held
    std::vector<entry>::iterator find(uint16_t tag);

    std::unique_ptr<stream> _s;
    mutex _mtx;
    condvar _cv;
    std::vector<entry> _held;
    session* _owner = nullptr;
    int _err = 0;          // sticky; set once the stream is unusable
    bool _sending = false; // one writer at a time: frames never interleave
    bool _rx_done = false;
    std::thread _rx;
};

int stream_tp::sleep(deadline d)
{
    if (d == deadline::max()) {
        _cv.wait(&_mtx);
        return 0;
    }
    if (osv::clock::uptime::now() >= d) {
        return ETIMEDOUT;
    }
    _cv.wait(&_mtx, d);
    return 0;
}

void stream_tp::fail(int err)
{
    if (!_err) {
        _err = err;
        _s->close(); // wakes the receiver and any blocked writer
    }
    _cv.wake_all();
}

std::vector<stream_tp::entry>::iterator stream_tp::find(uint16_t tag)
{
    for (auto it = _held.begin(); it != _held.end(); ++it) {
        if (it->r->tag == tag) {
            return it;
        }
    }
    return _held.end();
}

int stream_tp::submit(std::shared_ptr<request> r, deadline d)
{
    if (!r || !r->owner || r->tx.size() < 7) {
        return EINVAL;
    }
    WITH_LOCK(_mtx) {
        if (!_owner) {
            _owner = r->owner;
        }
        while (!_err && _sending) {
            if (sleep(d)) {
                return ETIMEDOUT; // nothing written
            }
        }
        if (_err) {
            return _err;
        }
        if (r->owner != _owner || find(r->tag) != _held.end()) {
            return EINVAL;
        }
        _held.push_back(entry{r, false});
        _sending = true;
    }
    // Write the whole frame. Progress from partial writes, EINTR and EAGAIN
    // is kept, and every wait is bounded by the same absolute d.
    const uint8_t* p = r->tx.data();
    const size_t len = r->tx.size();
    size_t off = 0;
    int rc = 0;
    while (off < len) {
        size_t n = 0;
        rc = _s->write(p + off, len - off, n);
        off += n;
        if (off < len && osv::clock::uptime::now() >= d) {
            rc = ETIMEDOUT;
            break;
        }
        if (off < len && (rc == EAGAIN || (!rc && !n))) {
            rc = _s->wait(false, d);
            if (rc == ETIMEDOUT) {
                continue; // the check above ends the loop
            }
        }
        // EAGAIN here moved the last bytes: the frame is complete.
        if (rc && rc != EINTR && rc != EAGAIN) {
            break;
        }
        rc = 0;
    }
    WITH_LOCK(_mtx) {
        _sending = false;
        auto it = _held.begin();
        while (it->r != r) {
            ++it;
        }
        if (!rc) {
            rc = _err;
        }
        if (rc) {
            // Never matched, so the receiver will not touch r: the session
            // keeps sole ownership. A partial frame desynchronizes the
            // stream, so any failure here fails the transport.
            _held.erase(it);
            fail(rc);
        } else {
            it->sent = true;
        }
        _cv.wake_all();
    }
    return rc;
}

// Read exactly n bytes. ECONNRESET at end of stream, even between frames.
int stream_tp::read_full(uint8_t* p, size_t n)
{
    size_t off = 0;
    while (off < n) {
        size_t got = 0;
        int rc = _s->read(p + off, n - off, got);
        off += got;
        if (!rc && !got) {
            return ECONNRESET;
        }
        if (rc == EAGAIN) {
            rc = _s->wait(true, deadline::max());
        }
        if (rc && rc != EINTR) {
            return rc;
        }
    }
    return 0;
}

// The only reader of the stream and the only caller of received() and
// retired() for written frames.
void stream_tp::receive()
{
    int err = 0;
    for (;;) {
        uint8_t hdr[7];
        err = read_full(hdr, sizeof hdr);
        if (err) {
            break;
        }
        const uint32_t size = le32(hdr);
        std::shared_ptr<request> r;
        WITH_LOCK(_mtx) {
            auto it = find(le16(hdr + 5));
            // An early reply to a frame still being written waits for the
            // writer, which finishes or fails by its deadline.
            while (!_err && it != _held.end() && !it->sent) {
                _cv.wait(&_mtx);
                it = find(le16(hdr + 5));
            }
            if (_err) {
                err = _err;
            } else if (it == _held.end() || size < 7
                       || size > it->r->rx.size()) {
                err = EPROTO; // unknown tag or a size no reply may have
            } else {
                r = it->r;
            }
        }
        if (err) {
            break;
        }
        std::copy(hdr, hdr + 7, r->rx.begin());
        err = read_full(r->rx.data() + 7, size - 7);
        if (err) {
            break;
        }
        std::shared_ptr<request> orig;
        WITH_LOCK(_mtx) {
            _held.erase(find(r->tag));
            // Rflush: the server sends nothing more for oldtag, so an
            // original still unanswered is done with its buffers.
            if (r->tx[4] == Tflush && r->tx.size() >= 9) {
                auto it = find(le16(&r->tx[7]));
                if (it != _held.end() && it->sent) {
                    orig = it->r;
                    _held.erase(it);
                }
            }
        }
        r->owner->received(*r, size);
        r->owner->retired(*r);
        if (orig) {
            orig->owner->retired(*orig);
        }
    }
    std::vector<std::shared_ptr<request>> left;
    session* owner;
    WITH_LOCK(_mtx) {
        fail(err);
        err = _err;
        owner = _owner;
        for (auto it = _held.begin(); it != _held.end();) {
            if (it->sent) {
                left.push_back(it->r);
                it = _held.erase(it);
            } else {
                ++it; // its writer removes it
            }
        }
    }
    if (owner) {
        owner->failed(err);
    }
    for (auto& r : left) {
        r->owner->retired(*r);
    }
    WITH_LOCK(_mtx) {
        _rx_done = true;
        _cv.wake_all();
    }
}

int stream_tp::shutdown(deadline d)
{
    WITH_LOCK(_mtx) {
        fail(ESHUTDOWN);
        while (!_rx_done || _sending) {
            if (sleep(d)) {
                return ETIMEDOUT;
            }
        }
    }
    if (_rx.joinable()) {
        _rx.join();
    }
    return 0;
}

}

std::unique_ptr<transport> stream_transport(std::unique_ptr<stream> s)
{
    return std::unique_ptr<transport>(new stream_tp(std::move(s)));
}

}
