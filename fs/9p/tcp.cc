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
#include <atomic>
#include <cerrno>
#include <cstring>
#include <vector>

#ifdef __OSV__
// Kernel socket API (errno returns, no descriptor); include order as in
// bsd/sys/kern/uipc_syscalls.cc.
#include <sys/cdefs.h>
#include <bsd/porting/netport.h>
#include <osv/fcntl.h>
#include <osv/file.h>
#include <osv/sched.hh>
#include <osv/uio.h>
#include <osv/socket.hh>
#include <osv/poll.h>
#include <bsd/sys/sys/param.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>
#include <bsd/sys/netinet/in.h>
#else
#include <thread>
#endif

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
#ifdef __OSV__
        // A kernel thread, not owned by (or stopped with) the application
        // that created the session.
        _rx = sched::thread::make_unique([this] { receive(); },
                                         sched::thread::attr().name("9p-rx"));
        _rx->start();
#else
        _rx = std::thread([this] { receive(); });
#endif
    }
    ~stream_tp() override
    {
        shutdown(deadline::max());
        join();
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
    void join();
    std::vector<entry>::iterator find(uint16_t tag);

    std::unique_ptr<stream> _s;
    mutex _mtx;
    condvar _cv;
    std::vector<entry> _held;
    session* _owner = nullptr;
    int _err = 0;          // sticky; set once the stream is unusable
    bool _sending = false; // one writer at a time: frames never interleave
    unsigned _inside = 0;  // threads in submit(); shutdown waits for them
    bool _rx_done = false;
    mutex _join_mtx; // the receiver never takes it
    bool _joined = false;
#ifdef __OSV__
    sched::thread::thread_unique_ptr _rx{nullptr, sched::thread::dispose};
#else
    std::thread _rx;
#endif
};

void stream_tp::join()
{
    // Once, even if several shutdown() calls race. Destruction concurrent
    // with shutdown() is excluded by the session contract, not handled here.
    WITH_LOCK(_join_mtx) {
        if (!_joined) {
#ifdef __OSV__
            _rx->join();
#else
            _rx.join();
#endif
            _joined = true;
        }
    }
}

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

// The errno a broken stream reports. Never one the session could read as
// "try again" or "still in progress" (a nonblocking socket's EAGAIN,
// EINTR, EINPROGRESS, EALREADY), and never 0.
int fatal(int err)
{
    switch (err) {
    case 0:
    case EAGAIN:
    case EINTR:
    case EINPROGRESS:
    case EALREADY:
        return EIO;
    }
    return err;
}

void stream_tp::fail(int err)
{
    if (!_err) {
        _err = fatal(err);
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
        int rc = 0;
        ++_inside;
        while (!_err && _sending && !rc) {
            rc = sleep(d); // ETIMEDOUT: nothing written
        }
        if (!rc) {
            rc = _err;
        }
        if (!rc && (r->owner != _owner || find(r->tag) != _held.end())) {
            rc = EINVAL;
        }
        if (rc) {
            --_inside;
            _cv.wake_all();
            return rc;
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
            rc = _err;
        } else {
            it->sent = true;
        }
        --_inside;
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
        while (!_rx_done || _inside) {
            if (sleep(d)) {
                return ETIMEDOUT;
            }
        }
    }
    join();
    return 0;
}

#ifdef __OSV__
// A connected TCP socket behind a socket_file that has no descriptor.
// SS_NBIO makes sosend/soreceive return EWOULDBLOCK (EAGAIN) instead of
// sleeping, with partial progress kept in the uio; waits go through
// do_poll with an absolute uptime deadline. Sends are serialized and the
// receiver is the only reader, so the sockbuf locks they take are not
// contended (soshutdown holds the receive one only briefly).
class socket_stream : public stream {
public:
    socket_stream(fileref fp, socket* so) : _fp(std::move(fp)), _so(so) {}
    int read(uint8_t* p, size_t n, size_t& done) override
    {
        iovec iov{p, n};
        uio u = io(iov, UIO_READ);
        int rc = soreceive(_so, nullptr, &u, nullptr, nullptr, nullptr);
        done = n - size_t(u.uio_resid);
        return rc;
    }
    int write(const uint8_t* p, size_t n, size_t& done) override
    {
        iovec iov{const_cast<uint8_t*>(p), n};
        uio u = io(iov, UIO_WRITE);
        int rc = sosend(_so, nullptr, &u, nullptr, nullptr, 0, nullptr);
        done = n - size_t(u.uio_resid);
        return rc;
    }
    int wait(bool in, deadline d) override
    {
        return poll_until(_fp, in ? POLLIN : POLLOUT, d);
    }
    // Shut both directions: blocked polls see readable and writable,
    // reads then return end of stream and writes EPIPE. The socket itself
    // is closed when _fp is released, after the receiver has been joined.
    void close() override
    {
        if (!_closed.exchange(true)) {
            soshutdown(_so, SHUT_RDWR);
        }
    }
    static int poll_until(const fileref& fp, int events, deadline d)
    {
        std::vector<poll_file> pf;
        pf.emplace_back(fp, events);
        if (!do_poll(pf, file::timeout_t(d))
            && osv::clock::uptime::now() >= d) {
            return ETIMEDOUT;
        }
        return 0;
    }

private:
    static uio io(iovec& iov, uio_rw rw)
    {
        uio u;
        u.uio_iov = &iov;
        u.uio_iovcnt = 1;
        u.uio_offset = 0;
        u.uio_resid = ssize_t(iov.iov_len);
        u.uio_rw = rw;
        return u;
    }
    fileref _fp;
    socket* _so;
    std::atomic<bool> _closed{false};
};
#endif

}

std::unique_ptr<transport> stream_transport(std::unique_ptr<stream> s)
{
    return std::unique_ptr<transport>(new stream_tp(std::move(s)));
}

#ifdef __OSV__
int connect_tcp(const sockaddr* addr, socklen_t len, deadline d,
                std::unique_ptr<transport>& out)
{
    // Linux sockaddr_in is family[2] port[2] addr[4] zero[8]; the BSD form
    // replaces family with len[1] family[1] (as linux_getsockaddr does).
    // AF_INET is 2 in both.
    bsd_sockaddr_in sin;
    uint16_t family;
    if (!addr || len < sizeof sin) {
        return EINVAL;
    }
    std::memcpy(&family, addr, sizeof family);
    if (family != AF_INET) {
        return EAFNOSUPPORT;
    }
    std::memcpy(&sin, addr, sizeof sin);
    sin.sin_len = sizeof sin;
    sin.sin_family = AF_INET;
    socket* so;
    int rc = socreate(AF_INET, &so, SOCK_STREAM, IPPROTO_TCP, nullptr,
                      nullptr);
    if (rc) {
        return rc;
    }
    socketref ref(so);
    fileref fp;
    try {
        fp = make_file<socket_file>(FREAD | FWRITE, std::move(ref));
    } catch (int e) {
        return e; // ref still owns so and closes it
    }
    // Releasing fp on any failure below closes the socket, abandoning a
    // connection still in progress.
    SOCK_LOCK(so);
    so->so_state |= SS_NBIO;
    SOCK_UNLOCK(so);
    // soconnect only starts the handshake; wait for it, bounded by d,
    // instead of kern_connect's unbounded sleep.
    rc = soconnect(so, reinterpret_cast<bsd_sockaddr*>(&sin), nullptr);
    while (!rc) {
        SOCK_LOCK(so);
        bool connecting = (so->so_state & SS_ISCONNECTING) && !so->so_error;
        if (!connecting) {
            rc = so->so_error;
            so->so_error = 0;
            if (!rc && !(so->so_state & SS_ISCONNECTED)) {
                rc = ECONNREFUSED;
            }
        }
        SOCK_UNLOCK(so);
        if (!connecting) {
            break;
        }
        // Checked before every poll, so no readiness report can carry the
        // loop past d.
        if (osv::clock::uptime::now() >= d) {
            rc = ETIMEDOUT;
            break;
        }
        rc = socket_stream::poll_until(fp, POLLOUT, d);
    }
    if (rc) {
        return fatal(rc);
    }
    try {
        out = stream_transport(std::unique_ptr<stream>(
            new socket_stream(std::move(fp), so)));
    } catch (const std::bad_alloc&) {
        return ENOMEM;
    }
    return 0;
}
#endif

}
