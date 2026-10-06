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

    ~script() override
    {
        if (inside) {
            destroyed_inside = true;
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
    int wait(bool rd, p9::deadline d) override
    {
        if (!rd && wpause.count()) {
            std::this_thread::sleep_for(wpause);
        }
        WITH_LOCK(mtx) {
            while (!closed
                   && (rd ? in.empty() && !eof && !reset : wblock)) {
                if (osv::clock::uptime::now() >= d) {
                    return ETIMEDOUT;
                }
                if (d == p9::deadline::max()) {
                    cv.wait(&mtx);
                } else {
                    cv.wait(&mtx, d);
                }
            }
        }
        return 0;
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

int main()
{
    fragmentation();
    interleaving();
    send_deadline();
    failures();
    flush_retires();
    shutdown_joins();
    std::printf("SUMMARY: %u tcp checks, 0 failures\n", checks.load());
    return 0;
}
