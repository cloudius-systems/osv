/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Concurrency checks of fs/9p/session.{hh,cc} through a deterministic
// in-process fake transport: real threads call session::rpc while the test
// thread plays server, choosing exactly when each reply and each buffer
// retirement happens. Protocol facts: Plan 9 intro(5), flush(5), version(5)
// (https://9fans.github.io/plan9port/man/man9/), Linux v6.12
// include/net/9p/9p.h for opcode numbers. No implementation code imported.

#include <fs/9p/session.hh>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

using p9::bytes;
using p9::frame;
using p9::request;
using namespace std::chrono;

static unsigned checks;

static void require(bool ok, const char* name)
{
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s (check %u)\n", name, checks);
        std::exit(1);
    }
}

static p9::deadline after(milliseconds ms)
{
    return osv::clock::uptime::now() + ms;
}

static void put32(bytes& b, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        b.push_back(uint8_t(v >> 8 * i));
    }
}

static uint32_t get32(const bytes& b, size_t at)
{
    return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8
        | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}

static frame decoded(const request& r);

// Holds every submitted request until the test answers and retires it, or
// with autoreply answers and retires it inside submit() (a transport that
// completes synchronously).
class fake : public p9::transport {
public:
    int fail_submit = 0;
    bool autoreply = false;
    uint32_t version_msize = p9::min_msize;
    std::string version = "9P2000.L";
    uint16_t walk_short = 0; // autoreply walks this many names fewer
    std::vector<std::shared_ptr<request>> held; // submitted, not retired
    std::atomic<unsigned> submitted{0};

    int submit(std::shared_ptr<request> r, p9::deadline) override
    {
        WITH_LOCK(mtx) {
            if (fail_submit) {
                return fail_submit;
            }
            ++submitted;
            if (!autoreply) {
                held.push_back(r);
                cv.wake_all();
                return 0;
            }
        }
        reply(*r, answer(decoded(*r)));
        r->owner->retired(*r);
        return 0;
    }
    frame answer(const frame& t)
    {
        frame f{uint8_t(t.type + 1), t.tag, {}};
        switch (t.type) {
        case p9::Tversion:
            put32(f.body, version_msize);
            f.body.push_back(uint8_t(version.size()));
            f.body.push_back(0);
            f.body.insert(f.body.end(), version.begin(), version.end());
            break;
        case p9::Tattach:
            f.body.assign(13, 0);
            break;
        case p9::Twalk: {
            uint16_t n = uint16_t(t.body[8] | t.body[9] << 8) - walk_short;
            f.body = {uint8_t(n), uint8_t(n >> 8)};
            f.body.resize(2 + 13 * n);
            break;
        }
        case p9::Tread:
            f.body.assign(4, 0);
            break;
        }
        return f;
    }
    int shutdown(p9::deadline) override
    {
        std::vector<std::shared_ptr<request>> all;
        WITH_LOCK(mtx) {
            all.swap(held);
        }
        for (auto& r : all) {
            r->owner->retired(*r);
        }
        return 0;
    }
    // Wait (bounded) until n requests have been submitted in total.
    bool await(unsigned n)
    {
        auto d = after(milliseconds(5000));
        WITH_LOCK(mtx) {
            while (submitted < n) {
                if (osv::clock::uptime::now() >= d) {
                    return false;
                }
                cv.wait(&mtx, d);
            }
        }
        return true;
    }
    size_t pending()
    {
        WITH_LOCK(mtx) {
            return held.size();
        }
    }
    std::shared_ptr<request> at(unsigned i)
    {
        WITH_LOCK(mtx) {
            return held.at(i);
        }
    }
    // Write reply f into r's buffer and deliver it.
    void reply(request& r, const frame& f)
    {
        bytes wire;
        require(p9::encode(f, 65536, wire) == 0, "encode reply");
        require(wire.size() <= r.rx.size(), "reply fits rx");
        std::copy(wire.begin(), wire.end(), r.rx.begin());
        r.owner->received(r, wire.size());
    }
    void retire(const std::shared_ptr<request>& r)
    {
        WITH_LOCK(mtx) {
            for (auto it = held.begin(); it != held.end(); ++it) {
                if (*it == r) {
                    held.erase(it);
                    break;
                }
            }
        }
        r->owner->retired(*r);
    }
    mutex mtx;
    condvar cv;
};

static frame decoded(const request& r)
{
    frame f;
    require(p9::decode(r.tx.data(), r.tx.size(), 65536, f) == 0,
            "tx decodes");
    return f;
}

static frame sent(const request& r)
{
    frame f = decoded(r);
    require(f.tag == r.tag, "tx carries the admitted tag");
    return f;
}

// Tread(fid, offset 0, count 0): the generic data request in these tests.
static bytes read_body(uint32_t fid)
{
    bytes b;
    put32(b, fid);
    put32(b, 0);
    put32(b, 0);
    put32(b, 0);
    return b;
}

static frame rread(uint16_t tag)
{
    return frame{p9::Rread, tag, {0, 0, 0, 0}};
}

// One call on its own thread; result read after join.
struct caller {
    int rc = -1;
    frame reply{};
    std::atomic<bool> done{false};
    std::thread th;
    caller(p9::session& s, uint8_t type, bytes body, p9::deadline d)
        : th([this, &s, type, body, d] {
              rc = s.rpc(type, body, reply, d);
              done = true;
          })
    {
    }
    explicit caller(std::function<int()> fn)
        : th([this, fn] {
              rc = fn();
              done = true;
          })
    {
    }
    void join() { th.join(); }
    // True once rpc has returned, polling for at most ms.
    bool returns_within(milliseconds ms)
    {
        auto d = after(ms);
        while (!done && osv::clock::uptime::now() < d) {
            std::this_thread::sleep_for(milliseconds(1));
        }
        return done;
    }
};

static uint32_t root; // fid attached by make()

// A negotiated session with one attached fid (root), autoreply off.
static std::unique_ptr<p9::session> make(fake*& f,
                                         size_t ceiling = p9::alloc_ceiling,
                                         bool attach = true)
{
    f = new fake;
    std::unique_ptr<p9::session> s(
        new p9::session(std::unique_ptr<p9::transport>(f), ceiling));
    f->autoreply = true;
    require(s->negotiate(65536, p9::op_deadline()) == 0, "negotiate");
    require(s->msize() == p9::min_msize, "msize from Rversion");
    bytes qid;
    if (attach) {
        require(s->attach("u", "", 0, root, qid, p9::op_deadline()) == 0,
                "attach root");
    }
    f->autoreply = false;
    f->submitted = 0;
    return s;
}

static void plain_rpc()
{
    fake* f;
    auto s = make(f);
    caller c(*s, p9::Tread, read_body(root), p9::op_deadline());
    require(f->await(1), "request submitted");
    auto r = f->at(0);
    frame t = sent(*r);
    require(t.type == p9::Tread && t.body == read_body(root), "tx encoded");
    require(s->charged() > 0, "request charged while outstanding");
    f->reply(*r, rread(r->tag));
    require(c.returns_within(milliseconds(2000)),
            "reply wakes the caller before retirement");
    c.join();
    require(c.rc == 0 && c.reply.type == p9::Rread && c.reply.tag == r->tag,
            "reply returned to caller");
    require(s->busy() == 1 && s->charged() > 0,
            "slot and charge held until the transport retires the buffer");
    f->retire(r);
    require(s->busy() == 0 && s->charged() == 0, "retirement frees slot");
    require(r.use_count() == 1, "session dropped its request reference");

    caller e(*s, p9::Tread, read_body(root), p9::op_deadline());
    require(f->await(2), "second request submitted");
    r = f->at(0);
    f->reply(*r, frame{p9::Rlerror, r->tag, {2, 0, 0, 0}});
    f->retire(r);
    e.join();
    require(e.rc == ENOENT && e.reply.type == p9::Rlerror,
            "Rlerror mapped to errno");
    require(!s->dead() && s->busy() == 0, "session healthy after Rlerror");
    std::puts("PASS: plain rpc");
}

static void reply_after_caller_left()
{
    fake* f;
    auto s = make(f);
    caller c(*s, p9::Tread, read_body(root), after(milliseconds(200)));
    require(f->await(1), "request submitted");
    c.join();
    require(c.rc == ETIMEDOUT, "deadline bounds the wait");
    require(s->dead() != 0, "uncertain request fails the session");
    auto r = f->at(0);
    require(s->busy() == 1, "request outlives its caller until retired");
    // The transport still owns the buffers: write a late reply into them.
    f->reply(*r, rread(r->tag));
    require(sent(*r).type == p9::Tread, "tx intact after caller returned");
    f->retire(r);
    require(s->busy() == 0 && s->charged() == 0, "late retirement frees");
    frame out;
    require(s->rpc(p9::Tread, read_body(root), out, p9::op_deadline())
                == s->dead(), "dead session refuses new rpc");
    require(f->submitted == 1, "nothing sent after failure");
    std::puts("PASS: reply after caller left");
}

static void admission()
{
    fake* f;
    auto s = make(f);
    std::deque<caller> cs;
    for (unsigned i = 0; i < p9::data_tags; i++) {
        cs.emplace_back(*s, p9::Tread, read_body(root), p9::op_deadline());
    }
    require(f->await(p9::data_tags), "32 requests in flight");
    frame out;
    auto t0 = osv::clock::uptime::now();
    require(s->rpc(p9::Tread, read_body(root), out, after(milliseconds(200)))
                == ETIMEDOUT, "33rd request times out waiting for a slot");
    require(osv::clock::uptime::now() - t0 >= milliseconds(200),
            "33rd waited until its deadline");
    require(f->submitted == p9::data_tags && !s->dead(),
            "33rd sent nothing and the session is healthy");
    caller late(*s, p9::Tread, read_body(root), p9::op_deadline());
    std::this_thread::sleep_for(milliseconds(50));
    require(f->submitted == p9::data_tags, "waiter blocked while full");
    auto r = f->at(0);
    f->reply(*r, rread(r->tag));
    std::this_thread::sleep_for(milliseconds(50));
    require(f->submitted == p9::data_tags,
            "reply alone does not free a slot");
    f->retire(r);
    require(f->await(p9::data_tags + 1), "retirement admits the waiter");
    while (f->pending()) {
        r = f->at(0);
        f->reply(*r, rread(r->tag));
        f->retire(r);
    }
    late.join();
    for (auto& c : cs) {
        c.join();
        require(c.rc == 0, "every admitted request completed once");
    }
    require(late.rc == 0, "late waiter completed");
    require(s->busy() == 0 && s->charged() == 0 && !s->dead(), "drained");
    std::puts("PASS: admission");
}

static void ceiling()
{
    // Room for exactly two Tread requests' buffers: 7 + 16 + msize each.
    const size_t one = 7 + 16 + p9::min_msize;
    fake* f;
    auto s = make(f, 2 * one + one / 2);
    caller a(*s, p9::Tread, read_body(root), p9::op_deadline());
    caller b(*s, p9::Tread, read_body(root), p9::op_deadline());
    require(f->await(2), "two requests within the ceiling");
    require(s->charged() == 2 * one, "tx and rx both charged");
    caller c(*s, p9::Tread, read_body(root), p9::op_deadline());
    std::this_thread::sleep_for(milliseconds(100));
    require(f->submitted == 2, "third waits for budget");
    auto r = f->at(0);
    f->reply(*r, rread(r->tag));
    f->retire(r);
    require(f->await(3), "refund admits the third");
    while (f->pending()) {
        r = f->at(0);
        f->reply(*r, rread(r->tag));
        f->retire(r);
    }
    a.join();
    b.join();
    c.join();
    require(a.rc == 0 && b.rc == 0 && c.rc == 0, "all completed");
    // A Twrite whose buffers alone exceed the ceiling is refused at once.
    fake* g;
    auto tiny = make(g, 7 + 17 + p9::min_msize); // fits the attach only
    bytes w = read_body(root);
    w.resize(16 + 100);
    w[12] = 100;
    frame out;
    require(tiny->rpc(p9::Twrite, w, out, p9::op_deadline()) == ENOMEM,
            "request larger than the ceiling refused");
    require(g->submitted == 0 && !tiny->dead(), "nothing sent");
    std::puts("PASS: ceiling");
}

static void contract()
{
    fake* f;
    auto s = make(f);
    caller c(*s, p9::Tread, read_body(root), p9::op_deadline());
    require(f->await(1), "submitted");
    auto r = f->at(0);
    f->reply(*r, rread(r->tag));
    c.join();
    require(c.rc == 0, "first reply honored");
    f->reply(*r, rread(r->tag));
    require(s->dead() == EPROTO, "second delivery on one request fails");
    f->retire(r);
    require(s->busy() == 0 && s->charged() == 0, "retired after failure");

    auto s2 = make(f);
    f->fail_submit = ECONNRESET;
    frame out;
    require(s2->rpc(p9::Tread, read_body(root), out, p9::op_deadline())
                == ECONNRESET, "submit failure returned");
    require(s2->dead() == ECONNRESET && s2->busy() == 0
            && s2->charged() == 0, "submit failure fails and frees");
    std::puts("PASS: transport contract");
}

static void many()
{
    // Sequential traffic reusing slots on one session.
    fake* f;
    auto s = make(f);
    for (unsigned i = 0; i < 300; i++) {
        caller c(*s, p9::Tread, read_body(root), p9::op_deadline());
        require(f->await(i + 1), "submitted");
        auto r = f->at(0);
        f->reply(*r, rread(r->tag));
        f->retire(r);
        c.join();
        require(c.rc == 0, "sequential rpc");
    }
    require(!s->dead() && s->busy() == 0, "no residue");
    std::puts("PASS: sequential");
}

static void negotiation()
{
    fake* f = new fake;
    p9::session s{std::unique_ptr<p9::transport>(f)};
    uint32_t fid;
    bytes qid;
    require(s.attach("u", "", 0, fid, qid, p9::op_deadline()) == ENOTCONN,
            "attach before negotiate refused");
    require(s.fids() == 0 && f->submitted == 0, "nothing sent or held");
    require(s.negotiate(p9::min_msize - 1, p9::op_deadline()) == EINVAL
            && s.negotiate(65537, p9::op_deadline()) == EINVAL,
            "offered msize bounded");
    f->version_msize = 8192;
    caller c([&] { return s.negotiate(65536, p9::op_deadline()); });
    require(f->await(1), "Tversion submitted");
    auto r = f->at(0);
    frame t = decoded(*r);
    require(t.type == p9::Tversion && t.tag == p9::notag,
            "Tversion on NOTAG");
    require(t.body.size() == 14 && get32(t.body, 0) == 65536
            && std::string(t.body.begin() + 6, t.body.end()) == "9P2000.L",
            "Tversion offers msize and 9P2000.L");
    require(s.negotiate(65536, p9::op_deadline()) == EALREADY,
            "concurrent negotiate refused");
    f->reply(*r, f->answer(t));
    f->retire(r);
    c.join();
    require(c.rc == 0 && s.msize() == 8192, "server msize adopted");
    require(s.negotiate(65536, p9::op_deadline()) == EALREADY,
            "negotiate once");
    require(s.busy() == 0 && s.charged() == 0, "Tversion not charged");

    struct bad { std::string v; uint32_t m; int rc; } cases[] = {
        {"9P2000", 8192, EPROTONOSUPPORT},
        {"9P2000.u", 8192, EPROTONOSUPPORT},
        {"9P2000.L", p9::min_msize - 1, EPROTO},
        {"9P2000.L", 65537, EPROTO},
    };
    for (auto& b : cases) {
        fake* g = new fake;
        p9::session u{std::unique_ptr<p9::transport>(g)};
        g->autoreply = true;
        g->version = b.v;
        g->version_msize = b.m;
        require(u.negotiate(65536, p9::op_deadline()) == b.rc,
                "bad Rversion refused");
        require(u.dead() == b.rc && u.msize() == 0, "and fails the session");
    }

    fake* h = new fake;
    p9::session v{std::unique_ptr<p9::transport>(h)};
    require(v.negotiate(65536, after(milliseconds(100))) == ETIMEDOUT,
            "unanswered Tversion times out");
    require(v.dead() == ETIMEDOUT && v.busy() == 0, "and fails the session");
    require(h->pending() == 1, "buffer still owned by the transport");
    h->retire(h->at(0));
    std::puts("PASS: negotiation");
}

static void fids()
{
    fake* f;
    auto s = make(f);
    f->autoreply = true;
    require(s->fids() == 1, "root live");
    uint32_t a, b;
    bytes qids;
    std::vector<std::string> two{"x", "y"};
    require(s->walk(root, two, a, qids, p9::op_deadline()) == 0
            && a != root && qids.size() == 26, "walk creates newfid");
    require(s->fids() == 2, "two live");
    f->walk_short = 1;
    require(s->walk(root, two, b, qids, p9::op_deadline()) == ENOENT,
            "partial walk is ENOENT");
    require(s->fids() == 2, "partial walk released its newfid");
    f->walk_short = 0;
    require(s->walk(a, {}, b, qids, p9::op_deadline()) == 0 && b != a,
            "zero-name walk clones");
    require(s->walk(12345, two, b, qids, p9::op_deadline()) == EBADF,
            "walk from a dead fid refused");
    require(s->clunk(b, p9::op_deadline()) == 0 && s->fids() == 2,
            "clunk releases");
    require(s->clunk(b, p9::op_deadline()) == EBADF, "clunk once");
    frame out;
    require(s->rpc(p9::Tread, read_body(b), out, p9::op_deadline()) == EBADF,
            "rpc on a clunked fid refused");
    require(s->rpc(p9::Tclunk, read_body(a), out, p9::op_deadline()) == EINVAL,
            "Tclunk only through clunk()");

    // A fid is pinned while a request names it.
    f->autoreply = false;
    f->submitted = 0;
    caller rd(*s, p9::Tread, read_body(a), p9::op_deadline());
    require(f->await(1), "read submitted");
    require(s->clunk(a, p9::op_deadline()) == EBUSY, "pinned fid not clunked");
    auto r = f->at(0);
    f->reply(*r, rread(r->tag));
    rd.join();
    require(rd.rc == 0, "read done");
    // Pins end with the reply; the buffer is still held by the transport.
    caller cl([&] { return s->clunk(a, p9::op_deadline()); });
    require(f->await(2), "clunk submitted after the reply");
    auto c = f->at(1);
    require(sent(*c).type == p9::Tclunk, "Tclunk sent");
    require(s->rpc(p9::Tread, read_body(a), out, p9::op_deadline()) == EBADF,
            "no new pin while clunking");
    require(s->walk(a, {}, b, qids, p9::op_deadline()) == EBADF,
            "no walk from a clunking fid");
    require(s->clunk(a, p9::op_deadline()) == EBADF, "no second clunk");
    require(s->fids() == 2, "clunking fid not yet released");
    f->reply(*c, frame{p9::Rlerror, c->tag, {5, 0, 0, 0}});
    cl.join();
    require(cl.rc == EIO, "clunk error returned");
    require(s->fids() == 1, "clunk releases the fid whatever the reply");
    f->retire(r);
    f->retire(c);
    f->autoreply = true;
    require(s->walk(root, {}, a, qids, p9::op_deadline()) == 0, "new a");
    f->autoreply = false;

    // A clunk that cannot be sent (no slot before its deadline) leaves the
    // fid live: the server still holds it.
    f->submitted = 0;
    std::deque<caller> busy;
    for (unsigned i = 0; i < p9::data_tags; i++) {
        busy.emplace_back(*s, p9::Tread, read_body(root), p9::op_deadline());
    }
    require(f->await(p9::data_tags), "slots full");
    require(s->walk(root, {}, b, qids, after(milliseconds(1))) == ETIMEDOUT,
            "walk without a slot times out");
    unsigned before = s->fids();
    f->autoreply = true;
    std::thread drainer([&] {
        std::this_thread::sleep_for(milliseconds(300));
        while (f->pending()) {
            auto q = f->at(0);
            f->reply(*q, rread(q->tag));
            f->retire(q);
        }
    });
    require(s->clunk(a, after(milliseconds(1))) == ETIMEDOUT,
            "clunk without a slot times out");
    require(s->fids() == before, "unsent clunk keeps the fid");
    drainer.join();
    for (auto& c : busy) {
        c.join();
    }
    require(s->clunk(a, p9::op_deadline()) == 0 && s->fids() == before - 1,
            "fid still clunkable");

    // Exhaustion: max_fids reserved or live, then ENFILE, then recovery.
    f->autoreply = true;
    std::vector<uint32_t> all;
    while (s->fids() < p9::max_fids) {
        require(s->walk(root, {}, b, qids, p9::op_deadline()) == 0, "clone");
        all.push_back(b);
    }
    require(s->walk(root, {}, b, qids, p9::op_deadline()) == ENFILE,
            "fid table full");
    uint32_t x;
    require(s->attach("u", "", 0, x, qids, p9::op_deadline()) == ENFILE,
            "attach also refused");
    require(s->clunk(all.back(), p9::op_deadline()) == 0
            && s->walk(root, {}, b, qids, p9::op_deadline()) == 0,
            "a clunk makes room");
    require(!s->dead() && s->busy() == 0, "healthy");
    std::puts("PASS: fids");
}

int main()
{
    negotiation();
    fids();
    plain_rpc();
    reply_after_caller_left();
    admission();
    ceiling();
    contract();
    many();
    std::printf("SUMMARY: %u session checks, 0 failures\n", checks);
    return 0;
}
