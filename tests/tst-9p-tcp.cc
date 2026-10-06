/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Stream transport checks for fs/9p/tcp.{hh,cc}: the real session over a
// scripted in-memory stream whose far end is this test, playing server.
// The script injects partial writes, EINTR, EAGAIN, fragmentation,
// coalescing, end of stream and resets. Frame layout: Plan 9 intro(5);
// opcode numbers: Linux v6.12 include/net/9p/9p.h. No implementation code
// imported.

#include <fs/9p/tcp.hh>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#ifdef __OSV__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <osv/sched.hh>
#endif

using p9::bytes;
using p9::frame;
using namespace std::chrono;

static std::atomic<unsigned> checks{0};

static void require(bool ok, const char* name)
{
    unsigned n = ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s (check %u)\n", name, n);
        std::exit(1);
    }
}

static p9::deadline after(milliseconds ms)
{
    return osv::clock::uptime::now() + ms;
}

static long ms_since(p9::deadline t)
{
    return long(duration_cast<milliseconds>(osv::clock::uptime::now() - t)
                .count());
}

static void put32(bytes& b, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        b.push_back(uint8_t(v >> 8 * i));
    }
}

static uint32_t le32(const uint8_t* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16
        | uint32_t(p[3]) << 24;
}

static std::atomic<bool> destroyed_inside{false};

// The transport's end of a connection. The test reads what was written
// (wire) and supplies what is read (in).
class script : public p9::stream {
public:
    // Writes move at most wmax bytes; with wfault set every other write
    // also returns it (keeping its progress); wait(false) sleeps wpause;
    // wblock refuses writes until close().
    size_t wmax = SIZE_MAX;
    int wfault = 0;
    milliseconds wpause{0};
    bool wblock = false;
    // Reads move at most rmax bytes; with rfault every other read returns
    // it; reset makes reads fail; eof ends the stream once in is drained;
    // a read woken by close() returns close_lag later.
    size_t rmax = SIZE_MAX;
    int rfault = 0;
    int reset = 0;
    bool eof = false;
    milliseconds close_lag{0};

    bytes wire;
    size_t cursor = 0; // next unparsed byte of wire
    std::deque<uint8_t> in;
    bool closed = false;
    std::atomic<int> inside{0};
    mutex mtx;
    condvar cv;

    // Released while a transport thread is still inside it: fail now,
    // before that thread touches freed memory.
    ~script() override
    {
        if (inside) {
            destroyed_inside = true;
            require(false, "stream released only after the receiver exits");
        }
    }
    int read(uint8_t* p, size_t n, size_t& done) override
    {
        done = 0;
        ++inside;
        int rc = 0;
        bool lag = false;
        WITH_LOCK(mtx) {
            if (closed) {
                lag = true;
            } else if (reset) {
                rc = reset;
            } else if (in.empty()) {
                rc = eof ? 0 : EAGAIN;
            } else {
                done = std::min({n, rmax, in.size()});
                std::copy(in.begin(), in.begin() + done, p);
                in.erase(in.begin(), in.begin() + done);
                if (rfault && (rflip = !rflip)) {
                    rc = rfault;
                }
            }
        }
        if (lag) {
            std::this_thread::sleep_for(close_lag);
        }
        --inside;
        return rc;
    }
    int write(const uint8_t* p, size_t n, size_t& done) override
    {
        done = 0;
        WITH_LOCK(mtx) {
            if (closed) {
                return EPIPE;
            }
            if (wblock) {
                return EAGAIN;
            }
            done = std::min(n, wmax);
            wire.insert(wire.end(), p, p + done);
            cv.wake_all();
            if (wfault && (wflip = !wflip)) {
                return wfault;
            }
        }
        return 0;
    }
    // A wait woken by close() also returns close_lag later.
    int wait(bool rd, p9::deadline d) override
    {
        if (!rd && wpause.count()) {
            std::this_thread::sleep_for(wpause);
        }
        ++inside;
        int rc = 0;
        bool lag = false;
        WITH_LOCK(mtx) {
            while (!closed
                   && (rd ? in.empty() && !eof && !reset : wblock)) {
                if (osv::clock::uptime::now() >= d) {
                    rc = ETIMEDOUT;
                    break;
                }
                if (d == p9::deadline::max()) {
                    cv.wait(&mtx);
                } else {
                    cv.wait(&mtx, d);
                }
            }
            lag = closed;
        }
        if (lag) {
            std::this_thread::sleep_for(close_lag);
        }
        --inside;
        return rc;
    }
    void close() override
    {
        WITH_LOCK(mtx) {
            closed = true;
            cv.wake_all();
        }
    }
    void give(const bytes& b)
    {
        WITH_LOCK(mtx) {
            in.insert(in.end(), b.begin(), b.end());
            cv.wake_all();
        }
    }
    void end(int err = 0)
    {
        WITH_LOCK(mtx) {
            if (err) {
                reset = err;
            } else {
                eof = true;
            }
            cv.wake_all();
        }
    }
    // The next complete T-frame written, decoded; false on stop or close.
    // Every frame must be well formed: interleaved writes would not be.
    bool take(frame& f, const std::atomic<bool>& stop)
    {
        WITH_LOCK(mtx) {
            for (;;) {
                size_t avail = wire.size() - cursor;
                if (avail >= 4) {
                    uint32_t size = le32(&wire[cursor]);
                    require(size >= 7 && size <= 65536,
                            "frames never interleave (size)");
                    if (avail >= size) {
                        require(p9::decode(&wire[cursor], size, 65536, f)
                                == 0, "frames never interleave (decode)");
                        cursor += size;
                        return true;
                    }
                }
                if (stop || closed) {
                    return false;
                }
                cv.wait(&mtx, after(milliseconds(10)));
            }
        }
        return false;
    }

private:
    bool rflip = false;
    bool wflip = false;
};

static bytes wire_of(const frame& f)
{
    bytes b;
    require(p9::encode(f, 65536, b) == 0, "encode reply");
    return b;
}

constexpr uint32_t msize = 8192;
constexpr uint16_t read_count = 4;

// The reply a well-behaved server sends to t. Rread carries read_count
// bytes: the low byte of the tag, then "xyz".
static frame answer(const frame& t)
{
    frame r{uint8_t(t.type + 1), t.tag, {}};
    switch (t.type) {
    case p9::Tversion:
        put32(r.body, msize);
        r.body.push_back(8);
        r.body.push_back(0);
        for (char c : std::string("9P2000.L")) {
            r.body.push_back(uint8_t(c));
        }
        break;
    case p9::Tattach:
        r.body.assign(13, 0);
        break;
    case p9::Tread:
        require(t.body.size() == 16, "Tread is exactly 23 bytes");
        put32(r.body, read_count);
        r.body.push_back(uint8_t(t.tag));
        r.body.push_back('x');
        r.body.push_back('y');
        r.body.push_back('z');
        break;
    }
    return r;
}

// A session over a fresh script, plus a server thread answering frames
// while `pick` says so (pick may also stop the server by returning false
// after setting stop). The script belongs to the transport.
struct rig {
    script* s;
    std::unique_ptr<p9::session> ses;
    std::atomic<bool> stop{false};
    std::thread srv;
    std::atomic<unsigned> answered{0};
    uint32_t fid = p9::nofid;

    explicit rig(std::function<bool(const frame&)> pick = nullptr)
    {
        s = new script;
        ses.reset(new p9::session(
            p9::stream_transport(std::unique_ptr<p9::stream>(s))));
        srv = std::thread([this, pick] {
            frame t;
            while (s->take(t, stop)) {
                if (!pick || pick(t)) {
                    s->give(wire_of(answer(t)));
                    ++answered;
                }
            }
        });
    }
    ~rig()
    {
        stop = true;
        srv.join();
    }
    void start()
    {
        require(ses->negotiate(msize, after(milliseconds(5000))) == 0,
                "negotiate over the stream");
        bytes qid;
        require(ses->attach("u", "/x", 0, fid, qid, after(milliseconds(5000)))
                == 0, "attach over the stream");
    }
    // Stop answering; the next take() returns false.
    void quiet()
    {
        stop = true;
        srv.join();
        stop = false;
        srv = std::thread([] {});
    }
};

// busy() reaches 0 shortly after a caller returns: the transport retires
// buffers after delivering the reply. Wait for it, bounded.
static bool settled(p9::session& s)
{
    auto d = after(milliseconds(2000));
    while (s.busy() && osv::clock::uptime::now() < d) {
        std::this_thread::sleep_for(milliseconds(1));
    }
    return s.busy() == 0;
}

static bytes tread_body(uint32_t fid)
{
    bytes b;
    put32(b, fid);
    put32(b, 0);
    put32(b, 0); // offset
    put32(b, read_count);
    return b;
}

// rpc(Tread) on another thread; done once it returned.
struct caller {
    std::atomic<bool> done{false};
    int rc = -1;
    frame reply{};
    std::thread t;
    caller(p9::session& s, uint32_t fid, milliseconds within)
    {
        t = std::thread([this, &s, fid, within] {
            rc = s.rpc(p9::Tread, tread_body(fid), reply, after(within));
            done = true;
        });
    }
    // Wait (bounded) for the return.
    bool finish(milliseconds within = milliseconds(5000))
    {
        auto d = after(within);
        while (!done && osv::clock::uptime::now() < d) {
            std::this_thread::sleep_for(milliseconds(1));
        }
        if (done) {
            t.join();
        }
        return done;
    }
    ~caller()
    {
        if (t.joinable()) {
            t.join();
        }
    }
};

static bool read_ok(const caller& c)
{
    const bytes& b = c.reply.body;
    return c.rc == 0 && c.reply.type == p9::Rread && b.size() == 8
        && le32(b.data()) == read_count && b[4] == uint8_t(c.reply.tag)
        && b[5] == 'x' && b[6] == 'y' && b[7] == 'z';
}

// Frames the server must not answer automatically, collected for the test.
struct held {
    mutex mtx;
    std::vector<frame> f;
    std::function<bool(const frame&)> pick()
    {
        return [this](const frame& t) {
            if (t.type != p9::Tread) {
                return true;
            }
            WITH_LOCK(mtx) {
                f.push_back(t);
            }
            return false;
        };
    }
    frame at(size_t i)
    {
        WITH_LOCK(mtx) {
            return f.at(i);
        }
    }
    bool await(size_t n)
    {
        auto d = after(milliseconds(5000));
        while (osv::clock::uptime::now() < d) {
            WITH_LOCK(mtx) {
                if (f.size() >= n) {
                    return true;
                }
            }
            std::this_thread::sleep_for(milliseconds(1));
        }
        return false;
    }
};

static void fragmentation()
{
    held h;
    rig r(h.pick());
    r.start();
    // Two replies coalesced into one read.
    {
        caller a(*r.ses, r.fid, milliseconds(5000));
        caller b(*r.ses, r.fid, milliseconds(5000));
        require(h.await(2), "two Treads written");
        bytes both = wire_of(answer(h.at(1)));
        bytes one = wire_of(answer(h.at(0)));
        both.insert(both.end(), one.begin(), one.end());
        r.s->give(both);
        require(a.finish() && b.finish(), "coalesced replies delivered");
        require(read_ok(a) && read_ok(b), "coalesced frames kept apart");
    }
    // One byte per read, every other read EINTR; then a reply split
    // across separate arrivals (reads see EAGAIN in between).
    for (int round = 0; round < 2; round++) {
        WITH_LOCK(r.s->mtx) {
            r.s->rmax = round ? SIZE_MAX : 1;
            r.s->rfault = round ? 0 : EINTR;
        }
        caller c(*r.ses, r.fid, milliseconds(5000));
        require(h.await(3 + round), "Tread written");
        bytes w = wire_of(answer(h.at(2 + round)));
        if (round == 0) {
            r.s->give(w);
        } else {
            for (size_t at : {size_t(0), size_t(2), size_t(9)}) {
                size_t to = at == 9 ? w.size() : at == 2 ? 9 : 2;
                r.s->give(bytes(w.begin() + at, w.begin() + to));
                std::this_thread::sleep_for(milliseconds(20));
                require(at == 9 || !c.done, "no reply before its last byte");
            }
        }
        require(c.finish() && read_ok(c),
                round ? "fragmented reply assembled"
                      : "1-byte EINTR reads assembled");
    }
    require(settled(*r.ses) && r.ses->dead() == 0, "session healthy");
    std::puts("PASS: fragmentation and coalescing");
}

// Many writers, 3-byte partial writes with EAGAIN or EINTR in between.
static void interleaving()
{
    for (int fault : {EAGAIN, EINTR}) {
        rig r;
        r.start();
        WITH_LOCK(r.s->mtx) {
            r.s->wmax = 3;
            r.s->wfault = fault;
        }
        r.s->wpause = milliseconds(1);
        std::vector<std::thread> ts;
        std::atomic<unsigned> good{0};
        for (int i = 0; i < 8; i++) {
            ts.emplace_back([&] {
                for (int j = 0; j < 10; j++) {
                    frame rep;
                    int rc = r.ses->rpc(p9::Tread, tread_body(r.fid), rep,
                                        after(milliseconds(10000)));
                    good += rc == 0 && rep.type == p9::Rread;
                }
            });
        }
        for (auto& t : ts) {
            t.join();
        }
        require(good == 80, "every partially sent request answered");
        require(r.answered == 82, "server parsed every frame whole");
    }
    std::puts("PASS: partial sends never interleave");
}

// The send deadline is absolute: progress does not extend it.
static void send_deadline()
{
    rig r;
    r.start();
    r.quiet();
    WITH_LOCK(r.s->mtx) {
        r.s->wmax = 1;
        r.s->wfault = EAGAIN;
    }
    r.s->wpause = milliseconds(50); // 23 bytes need over a second
    auto t0 = osv::clock::uptime::now();
    frame rep;
    int rc = r.ses->rpc(p9::Tread, tread_body(r.fid), rep,
                        after(milliseconds(300)));
    long took = ms_since(t0);
    require(rc == ETIMEDOUT, "slow send times out");
    require(took >= 250 && took < 900, "send stops at the absolute deadline");
    require(r.ses->dead() == ETIMEDOUT, "half-sent frame fails the session");
    std::puts("PASS: send deadline");
}

// End of stream, reset or a bad frame fails the session; every waiter
// returns the session error.
static void failures()
{
    struct fault {
        const char* name;
        int want;
        std::function<void(script&, const std::vector<frame>&)> act;
    };
    const fault cases[] = {
        {"partial frame then EOF", ECONNRESET,
         [](script& s, const std::vector<frame>& f) {
             bytes w = wire_of(answer(f[0]));
             s.give(bytes(w.begin(), w.end() - 2));
             s.end();
         }},
        {"EOF mid-header", ECONNRESET,
         [](script& s, const std::vector<frame>& f) {
             bytes w = wire_of(answer(f[0]));
             s.give(bytes(w.begin(), w.begin() + 3));
             s.end();
         }},
        {"EOF between frames", ECONNRESET,
         [](script& s, const std::vector<frame>&) { s.end(); }},
        {"reset", ECONNRESET,
         [](script& s, const std::vector<frame>&) { s.end(ECONNRESET); }},
        {"size below header", EPROTO,
         [](script& s, const std::vector<frame>& f) {
             bytes w = wire_of(answer(f[0]));
             w[0] = 6;
             s.give(w);
         }},
        {"size above msize", EPROTO,
         [](script& s, const std::vector<frame>& f) {
             bytes w = wire_of(answer(f[0]));
             bytes hdr(w.begin(), w.begin() + 7);
             hdr[0] = uint8_t(msize + 1);
             hdr[1] = uint8_t((msize + 1) >> 8);
             s.give(hdr);
         }},
        {"stream EINPROGRESS", EIO,
         [](script& s, const std::vector<frame>&) { s.end(EINPROGRESS); }},
        {"unknown tag", EPROTO,
         [](script& s, const std::vector<frame>& f) {
             frame a = answer(f[0]);
             a.tag = 0x7777;
             s.give(wire_of(a));
         }},
    };
    for (auto& c : cases) {
        held h;
        rig r(h.pick());
        r.start();
        caller a(*r.ses, r.fid, milliseconds(10000));
        caller b(*r.ses, r.fid, milliseconds(10000));
        caller d(*r.ses, r.fid, milliseconds(10000));
        require(h.await(3), "three Treads written");
        c.act(*r.s, {h.at(0)});
        bool all = a.finish() && b.finish() && d.finish();
        if (!all || a.rc != c.want || b.rc != c.want || d.rc != c.want) {
            std::fprintf(stderr, "%s: rc %d %d %d want %d\n", c.name, a.rc,
                         b.rc, d.rc, c.want);
        }
        require(all, "every waiter woken");
        require(a.rc == c.want && b.rc == c.want && d.rc == c.want,
                "waiters see the stream failure, not a reply");
        require(r.ses->dead() == c.want, "session failed once");
        frame rep;
        require(r.ses->rpc(p9::Tread, tread_body(r.fid), rep,
                           after(milliseconds(1000))) == c.want,
                "failed session refuses new requests");
        require(settled(*r.ses), "every request retired");
    }
    std::puts("PASS: EOF, reset and bad frames fail the session");
}

// A Tflush answered by Rflush alone retires the original's buffers; the
// session stays usable.
static void flush_retires()
{
    held h;
    rig r(h.pick());
    r.start();
    frame rep;
    auto t0 = osv::clock::uptime::now();
    int rc = r.ses->rpc(p9::Tread, tread_body(r.fid), rep,
                        after(milliseconds(200)));
    require(rc == ETIMEDOUT && ms_since(t0) < 2000, "canceled by Tflush");
    require(r.ses->dead() == 0, "Rflush keeps the session");
    require(settled(*r.ses) && r.ses->charged() == 0,
            "original retired by Rflush");
    std::puts("PASS: Rflush retires the original");
}

// shutdown() wakes a blocked writer and the receiver and joins the receiver
// before returning, so the stream outlives every call into it.
static void shutdown_joins()
{
    rig r;
    r.start();
    r.quiet();
    r.s->close_lag = milliseconds(200);
    caller waiting(*r.ses, r.fid, milliseconds(10000)); // sent, no reply
    auto d = after(milliseconds(2000));
    for (bool sent = false; !sent && osv::clock::uptime::now() < d;) {
        WITH_LOCK(r.s->mtx) {
            sent = r.s->wire.size() >= r.s->cursor + 23;
        }
        std::this_thread::sleep_for(milliseconds(1));
    }
    WITH_LOCK(r.s->mtx) {
        r.s->wblock = true;
    }
    caller blocked(*r.ses, r.fid, milliseconds(10000)); // cannot send
    std::this_thread::sleep_for(milliseconds(50));
    require(!waiting.done && !blocked.done, "both callers blocked");
    auto t0 = osv::clock::uptime::now();
    require(r.ses->shutdown(after(milliseconds(5000))) == 0,
            "shutdown retires everything");
    long took = ms_since(t0);
    require(r.s->inside == 0, "receiver joined before shutdown returns");
    require(took < 2000, "shutdown bounded");
    require(waiting.finish(milliseconds(1000))
            && blocked.finish(milliseconds(1000)), "blocked callers woken");
    require(waiting.rc == ESHUTDOWN && blocked.rc == ESHUTDOWN,
            "callers see ESHUTDOWN");
    r.ses.reset();
    require(!destroyed_inside, "stream released after the receiver exits");
    std::puts("PASS: shutdown wakes and joins");
}

// Destroying the transport directly, with the receiver blocked in the
// stream, joins the receiver before the stream is released.
static void destroy_joins()
{
    destroyed_inside = false;
    script* s = new script;
    s->close_lag = milliseconds(200);
    auto t = p9::stream_transport(std::unique_ptr<p9::stream>(s));
    auto d = after(milliseconds(2000));
    while (s->inside == 0 && osv::clock::uptime::now() < d) {
        std::this_thread::sleep_for(milliseconds(1));
    }
    require(s->inside == 1, "receiver blocked in the stream");
    t.reset();
    require(!destroyed_inside, "stream released only after the join");
    std::puts("PASS: destroy joins before release");
}

#ifdef __OSV__
// Receiver kernel threads that have not terminated.
static unsigned live_receivers()
{
    unsigned n = 0;
    sched::with_all_threads([&](sched::thread& t) {
        n += t.name() == "9p-rx"
            && t.get_status() != sched::thread::status::terminated;
    });
    return n;
}

// Real loopback TCP: a listener (application sockets, playing server)
// and the kernel transport from connect_tcp().
struct listener {
    int fd = -1;
    sockaddr_in addr{};
    explicit listener(int backlog = 4)
    {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        require(fd >= 0, "listener socket");
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        require(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr)
                == 0, "bind loopback");
        socklen_t l = sizeof addr;
        require(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &l)
                == 0, "getsockname");
        require(::listen(fd, backlog) == 0, "listen");
    }
    ~listener()
    {
        ::close(fd);
    }
    const sockaddr* sa() const
    {
        return reinterpret_cast<const sockaddr*>(&addr);
    }
    int accept_within(milliseconds ms)
    {
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, int(ms.count())) != 1) {
            return -1;
        }
        return ::accept(fd, nullptr, nullptr);
    }
};

static bool read_all(int fd, uint8_t* p, size_t n)
{
    while (n) {
        ssize_t k = ::read(fd, p, n);
        if (k <= 0) {
            return false;
        }
        p += k;
        n -= size_t(k);
    }
    return true;
}

static bool write_all(int fd, const bytes& b)
{
    return ::write(fd, b.data(), b.size()) == ssize_t(b.size());
}

// Read one T-frame from a server-side socket.
static bool read_frame(int fd, frame& f)
{
    uint8_t hdr[4];
    if (!read_all(fd, hdr, 4)) {
        return false;
    }
    uint32_t size = le32(hdr);
    if (size < 7 || size > 65536) {
        return false;
    }
    bytes b(hdr, hdr + 4);
    b.resize(size);
    return read_all(fd, b.data() + 4, size - 4)
        && p9::decode(b.data(), b.size(), 65536, f) == 0;
}

static void connect_errors()
{
    std::unique_ptr<p9::transport> t;
    sockaddr_in bad{};
    bad.sin_family = AF_INET6;
    require(p9::connect_tcp(reinterpret_cast<sockaddr*>(&bad), sizeof bad,
                            after(milliseconds(1000)), t) == EAFNOSUPPORT,
            "non-INET address refused");
    require(p9::connect_tcp(nullptr, 0, after(milliseconds(1000)), t)
            == EINVAL, "null address refused");
    require(p9::connect_tcp(reinterpret_cast<sockaddr*>(&bad), 4,
                            after(milliseconds(1000)), t) == EINVAL,
            "short address refused");
    // A closed loopback port answers the SYN with RST.
    sockaddr_in closed{};
    {
        listener l;
        closed = l.addr;
    }
    auto t0 = osv::clock::uptime::now();
    int rc = p9::connect_tcp(reinterpret_cast<sockaddr*>(&closed),
                             sizeof closed, after(milliseconds(5000)), t);
    require(rc == ECONNREFUSED, "closed port refused");
    require(ms_since(t0) < 2000 && !t, "refusal is prompt, no transport");
    std::puts("PASS: connect errors");
}

// 192.168.122.250 is inside run.py's user-mode network but unassigned:
// nothing answers ARP, so the handshake never completes and connect_tcp
// must give up at its deadline (TCP itself would wait 75 s).
static void connect_timeout()
{
    sockaddr_in silent{};
    silent.sin_family = AF_INET;
    silent.sin_port = htons(564);
    silent.sin_addr.s_addr = inet_addr("192.168.122.250");
    std::unique_ptr<p9::transport> t;
    auto t0 = osv::clock::uptime::now();
    int rc = p9::connect_tcp(reinterpret_cast<sockaddr*>(&silent),
                             sizeof silent, after(milliseconds(500)), t);
    long took = ms_since(t0);
    if (rc != ETIMEDOUT) {
        std::fprintf(stderr, "connect_timeout: rc %d after %ld ms\n", rc,
                     took);
    }
    require(rc == ETIMEDOUT, "unanswered connect times out");
    require(took >= 450 && took < 1500, "connect stops at its deadline");
    require(!t, "no transport after a timeout");
    std::puts("PASS: connect deadline");
}

// The real socket path: negotiate, attach, read, then the peer closes in
// the middle of a reply.
static void loopback_session()
{
    listener l;
    std::unique_ptr<p9::transport> t;
    require(p9::connect_tcp(l.sa(), sizeof l.addr, after(milliseconds(5000)),
                            t) == 0, "connect loopback");
    int c = l.accept_within(milliseconds(5000));
    require(c >= 0, "server accepted");
    std::atomic<bool> cut{false};
    std::thread srv([&] {
        frame f;
        bool half = false;
        while (read_frame(c, f)) {
            bytes w = wire_of(answer(f));
            if (half) {
                continue; // write side already shut
            }
            if (f.type == p9::Tread && cut) {
                // Half a reply, then end of stream.
                require(::write(c, w.data(), 9) == 9, "half reply sent");
                ::shutdown(c, SHUT_WR);
                half = true;
                continue;
            }
            // Split the reply over two segments.
            require(::write(c, w.data(), 3) == 3, "first fragment sent");
            std::this_thread::sleep_for(milliseconds(2));
            require(write_all(c, bytes(w.begin() + 3, w.end())),
                    "rest sent");
        }
    });
    p9::session s(std::move(t));
    require(s.negotiate(msize, after(milliseconds(5000))) == 0,
            "negotiate over TCP");
    require(s.msize() == msize, "msize from Rversion");
    uint32_t fid;
    bytes qid;
    require(s.attach("u", "/x", 0, fid, qid, after(milliseconds(5000))) == 0,
            "attach over TCP");
    for (int i = 0; i < 20; i++) {
        caller r(s, fid, milliseconds(5000));
        require(r.finish() && read_ok(r), "read over TCP");
    }
    cut = true;
    caller a(s, fid, milliseconds(10000));
    caller b(s, fid, milliseconds(10000));
    require(a.finish() && b.finish(), "waiters woken by EOF mid-frame");
    require(a.rc == ECONNRESET && b.rc == ECONNRESET,
            "EOF mid-frame fails the session");
    require(s.shutdown(after(milliseconds(5000))) == 0, "shutdown after EOF");
    ::close(c);
    srv.join();
    std::puts("PASS: loopback session");
}

// Shutdown with a receiver blocked in a real socket wait and a sender
// blocked on a full send buffer: both wake, the receiver is joined.
static void loopback_shutdown()
{
    listener l;
    std::unique_ptr<p9::transport> t;
    require(p9::connect_tcp(l.sa(), sizeof l.addr, after(milliseconds(5000)),
                            t) == 0, "connect loopback");
    int c = l.accept_within(milliseconds(5000));
    require(c >= 0, "server accepted");
    std::thread srv([&] {
        frame f;
        // Answer version (granting 64 KiB) and attach, then stop reading.
        for (int i = 0; i < 2 && read_frame(c, f); i++) {
            frame r = answer(f);
            if (f.type == p9::Tversion) {
                r.body[0] = 0;
                r.body[1] = 0;
                r.body[2] = 1; // msize 65536, little endian
                r.body[3] = 0;
            }
            require(write_all(c, wire_of(r)), "reply sent");
        }
    });
    auto s = std::unique_ptr<p9::session>(new p9::session(std::move(t)));
    require(s->negotiate(65536, after(milliseconds(5000))) == 0,
            "negotiate 64 KiB");
    uint32_t fid;
    bytes qid;
    require(s->attach("u", "/x", 0, fid, qid, after(milliseconds(5000)))
            == 0, "attach");
    srv.join();
    // Fill the socket buffers with large Twrites nobody reads.
    std::vector<std::thread> ws;
    std::atomic<unsigned> back{0};
    std::atomic<unsigned> shut{0};
    for (int i = 0; i < 8; i++) {
        ws.emplace_back([&] {
            bytes b;
            put32(b, fid);
            put32(b, 0);
            put32(b, 0);
            put32(b, 60000);
            b.resize(b.size() + 60000, 0x5a);
            frame rep;
            int rc = s->rpc(p9::Twrite, b, rep, after(milliseconds(20000)));
            shut += rc == ESHUTDOWN;
            ++back;
        });
    }
    std::this_thread::sleep_for(milliseconds(500));
    require(back == 0, "writers blocked on a full stream");
    require(live_receivers() == 1, "one receiver kernel thread");
    auto t0 = osv::clock::uptime::now();
    // EBUSY may be transient here: a writer woken with ESHUTDOWN may not
    // yet have relocked the session to mark its request unsent (N2b
    // shutdown() contract: call again).
    int rc = s->shutdown(after(milliseconds(5000)));
    for (int i = 0; rc == EBUSY && i < 100; i++) {
        std::this_thread::sleep_for(milliseconds(10));
        rc = s->shutdown(after(milliseconds(1000)));
    }
    require(rc == 0, "shutdown");
    require(live_receivers() == 0, "socket receiver joined by shutdown");
    require(ms_since(t0) < 3000, "shutdown wakes blocked socket I/O");
    for (auto& w : ws) {
        w.join();
    }
    require(back == 8 && shut == 8, "every writer returned ESHUTDOWN");
    s.reset();
    ::close(c);
    std::puts("PASS: loopback shutdown");
}

// The two cases below submit Twrite frames straight to a connect_tcp()
// transport while the loopback server does not read, so the transport's
// own socket writer meets a full send buffer. They depend on no buffer
// size: frames go out until one stops. The server never replies.

// Requests submitted straight to a transport still need an owning session
// for its completion calls. This one sits over a transport nobody uses and
// only absorbs them: the frames are never in its slots, so it records the
// retirements as a broken contract (EPROTO), which nothing here reads.
struct unused_tp : p9::transport {
    int submit(std::shared_ptr<p9::request>, p9::deadline) override
    {
        return EINVAL;
    }
    int shutdown(p9::deadline) override
    {
        return 0;
    }
};

// Twrite on tag i of tw_count bytes; data byte j is (i + j) % 251, so a
// lost, repeated or interleaved byte shows in the server's copy.
constexpr uint32_t tw_count = 60000;
constexpr unsigned tw_frames = 128; // far beyond any buffer autosizing

static std::shared_ptr<p9::request> twrite(p9::session* owner, uint16_t i)
{
    auto r = std::make_shared<p9::request>();
    r->owner = owner;
    r->tag = i;
    bytes b;
    put32(b, 1); // fid
    put32(b, 0);
    put32(b, 0); // offset
    put32(b, tw_count);
    for (uint32_t j = 0; j < tw_count; j++) {
        b.push_back(uint8_t((i + j) % 251));
    }
    require(p9::encode(frame{p9::Twrite, i, b}, 65536, r->tx) == 0,
            "encode Twrite");
    r->rx.resize(11);
    return r;
}

// Bytes the server has received and not read.
static int queued(int fd)
{
    int n = -1;
    require(::ioctl(fd, FIONREAD, &n) == 0, "FIONREAD");
    return n;
}

// One thread submits frames 0, 1, ... in order, each with a deadline
// `limit` after its call starts, until a call fails or it reaches upto.
// Per call: the result, how long it took, and the writer's context
// switches meanwhile (each wait in the socket normally costs one).
struct filler {
    std::vector<std::shared_ptr<p9::request>> f;
    std::vector<int> rc;
    std::vector<long> took;
    std::vector<uint64_t> waits;
    std::atomic<unsigned> done{0}; // calls returned
    std::atomic<unsigned> upto{tw_frames};
    std::atomic<bool> finished{false};
    std::thread t;
    filler(p9::transport& tp, p9::session* owner, milliseconds limit)
        : rc(tw_frames, -1), took(tw_frames, -1), waits(tw_frames, 0)
    {
        for (unsigned i = 0; i < tw_frames; i++) {
            f.push_back(twrite(owner, uint16_t(i)));
        }
        t = std::thread([this, &tp, limit] {
            for (unsigned i = 0; i < upto; i++) {
                sched::thread* me = sched::thread::current();
                uint64_t s0 = me->stat_switches.get();
                auto t0 = osv::clock::uptime::now();
                rc[i] = tp.submit(f[i], after(limit));
                took[i] = ms_since(t0);
                waits[i] = me->stat_switches.get() - s0;
                ++done;
                if (rc[i]) {
                    break;
                }
            }
            finished = true;
        });
    }
    ~filler()
    {
        t.join();
    }
    // Bounded wait for the thread to finish.
    bool finish(milliseconds within)
    {
        auto d = after(within);
        while (!finished && osv::clock::uptime::now() < d) {
            std::this_thread::sleep_for(milliseconds(1));
        }
        return finished;
    }
};

// (a) A frame that stops at EAGAIN on a full send buffer resumes as the
// server reads, keeps its partial progress, and arrives byte-exact. The
// server then reads slowly, so the three frames after it each start on a
// full buffer and cross several EAGAINs.
static void loopback_partial_send()
{
    p9::session owner(std::unique_ptr<p9::transport>(new unused_tp));
    listener l;
    std::unique_ptr<p9::transport> t;
    require(p9::connect_tcp(l.sa(), sizeof l.addr, after(milliseconds(5000)),
                            t) == 0, "connect loopback");
    int c = l.accept_within(milliseconds(5000));
    require(c >= 0, "server accepted");
    unsigned b = 0;
    {
        filler w(*t, &owner, milliseconds(30000));
        // Full once neither the calls returned nor the bytes queued at the
        // server have moved for 500 ms; frame b is then inside its call.
        auto d = after(milliseconds(20000));
        unsigned last_done = ~0u;
        int last_q = -1;
        auto since = osv::clock::uptime::now();
        bool full = false;
        while (!full && !w.finished && osv::clock::uptime::now() < d) {
            std::this_thread::sleep_for(milliseconds(50));
            unsigned n = w.done;
            int q = queued(c);
            if (n != last_done || q != last_q) {
                last_done = n;
                last_q = q;
                since = osv::clock::uptime::now();
            } else if (ms_since(since) >= 500) {
                full = true;
            }
        }
        b = w.done;
        require(full && !w.finished && b + 4 <= tw_frames,
                "send buffer fills until a frame stops");
        w.upto = b + 4;
        std::printf("loopback partial send: frame %u stopped, %d bytes "
                    "queued at the server\n", b, last_q);
        // Read late and slower than the writer: 4 KiB at a time, 1 ms
        // apart.
        bytes want, got;
        for (unsigned i = 0; i < b + 4; i++) {
            want.insert(want.end(), w.f[i]->tx.begin(), w.f[i]->tx.end());
        }
        got.reserve(want.size());
        uint8_t buf[4096];
        d = after(milliseconds(30000));
        while (got.size() < want.size() && osv::clock::uptime::now() < d) {
            pollfd p{c, POLLIN, 0};
            if (::poll(&p, 1, 100) != 1) {
                continue;
            }
            ssize_t k = ::read(c, buf, std::min(sizeof buf,
                                                want.size() - got.size()));
            if (k <= 0) {
                break;
            }
            got.insert(got.end(), buf, buf + k);
            std::this_thread::sleep_for(milliseconds(1));
        }
        require(w.finish(milliseconds(5000)), "writer returns");
        for (unsigned i = b; i < b + 4; i++) {
            std::printf("loopback partial send: frame %u returned %d after "
                        "%ld ms, %llu writer switches\n", i, w.rc[i],
                        w.took[i], (unsigned long long)w.waits[i]);
        }
        bool ok = w.done == b + 4;
        for (unsigned i = 0; i < b + 4; i++) {
            ok = ok && w.rc[i] == 0;
        }
        require(ok, "the stopped frame and the later ones complete");
        require(w.took[b] >= 450 && w.waits[b] >= 1,
                "the frame waited on the full buffer");
        uint64_t waits = 0;
        for (unsigned i = b; i < b + 4; i++) {
            waits += w.waits[i];
        }
        require(waits >= 4, "the later frames waited for buffer space");
        require(got == want, "every frame arrives byte-exact");
        std::this_thread::sleep_for(milliseconds(100));
        require(queued(c) == 0, "nothing sent after the stopped frame");
    }
    require(t->shutdown(after(milliseconds(5000))) == 0, "shutdown");
    t.reset();
    require(live_receivers() == 0, "receiver joined");
    ::close(c);
    std::puts("PASS: loopback partial send completes");
}

// (b) A frame that meets a full send buffer stops at its own deadline,
// limit after its call starts, within [limit, limit + 500 ms), with
// ETIMEDOUT, and the half-sent frame fails the transport.
static void loopback_send_deadline()
{
    constexpr milliseconds limit{500};
    p9::session owner(std::unique_ptr<p9::transport>(new unused_tp));
    listener l;
    std::unique_ptr<p9::transport> t;
    require(p9::connect_tcp(l.sa(), sizeof l.addr, after(milliseconds(5000)),
                            t) == 0, "connect loopback");
    int c = l.accept_within(milliseconds(5000));
    require(c >= 0, "server accepted");
    {
        filler w(*t, &owner, limit);
        // Every call is bounded by limit, so the writer must be back well
        // within this. If it is not, its send ignored the deadline: shut
        // the transport down to wake it, then fail.
        bool back = w.finish(milliseconds(10000));
        if (!back) {
            std::fprintf(stderr, "send deadline: writer still inside call %u"
                         " after 10 s\n", unsigned(w.done));
            t->shutdown(after(milliseconds(5000)));
            w.finish(milliseconds(5000));
        }
        require(back, "send on a full socket returns by its deadline");
        unsigned e = w.done - 1;
        std::printf("loopback send deadline: frame %u returned %d after %ld "
                    "ms, %d bytes queued at the server\n", e, w.rc[e],
                    w.took[e], queued(c));
        require(w.rc[e] == ETIMEDOUT, "send on a full buffer: ETIMEDOUT");
        require(w.took[e] >= limit.count()
                && w.took[e] < limit.count() + 500,
                "send stops at its deadline on a full socket");
        require(queued(c) > 0, "the server holds unread bytes");
        // A transport that kept going would write this to the same full
        // socket and return ETIMEDOUT only at its 1000 ms deadline; a
        // failed one returns its sticky error at once.
        auto t0 = osv::clock::uptime::now();
        int rc = t->submit(w.f[e], after(milliseconds(1000)));
        long took = ms_since(t0);
        std::printf("loopback send deadline: resubmit returned %d after %ld "
                    "ms\n", rc, took);
        require(rc == ETIMEDOUT && took < 100,
                "a half-sent frame fails the transport");
    }
    require(t->shutdown(after(milliseconds(5000))) == 0, "shutdown");
    t.reset();
    require(live_receivers() == 0, "receiver joined");
    ::close(c);
    std::puts("PASS: loopback send deadline");
}
#endif

// "socket" as the only argument runs just the real-socket groups.
int main(int argc, char** argv)
{
    if (argc < 2 || std::string(argv[1]) != "socket") {
        fragmentation();
        interleaving();
        send_deadline();
        failures();
        flush_retires();
        shutdown_joins();
        destroy_joins();
    }
#ifdef __OSV__
    connect_errors();
    connect_timeout();
    loopback_session();
    loopback_shutdown();
    loopback_partial_send();
    loopback_send_deadline();
#endif
    std::printf("SUMMARY: %u tcp checks, 0 failures\n", checks.load());
    return 0;
}
