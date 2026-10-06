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
    uint32_t lerror = 0; // autoreply answers Tread with Rlerror(lerror)
    std::vector<std::shared_ptr<request>> held; // submitted, not retired
    std::atomic<unsigned> submitted{0};

    // When set, the first submit checks from another thread that the
    // session lock is free (the mutex is recursive, so holding it across
    // submit would not deadlock the submitting thread itself).
    p9::session* probe = nullptr;
    std::atomic<bool> probed{false};
    bool probe_ok = false;
    std::thread prober;

    int submit(std::shared_ptr<request> r, p9::deadline) override
    {
        if (probe) {
            p9::session* sp = probe;
            probe = nullptr;
            prober = std::thread([this, sp] {
                sp->dead();
                probed = true;
            });
            auto d = after(milliseconds(1000));
            while (!probed && osv::clock::uptime::now() < d) {
                std::this_thread::sleep_for(milliseconds(1));
            }
            probe_ok = probed;
        }
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
            if (lerror) {
                f.type = p9::Rlerror;
                f.body.clear();
                put32(f.body, lerror);
            }
            break;
        }
        return f;
    }
    bool stuck = false; // shutdown cannot retire (device still owns DMA)
    int shutdown(p9::deadline) override
    {
        if (stuck) {
            return ETIMEDOUT;
        }
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
    void failed_from_transport(p9::session& s, int err) { s.failed(err); }
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
                                         bool attach = true,
                                         osv::clock::uptime::duration slot
                                             = p9::flush_slot_timeout)
{
    f = new fake;
    std::unique_ptr<p9::session> s(
        new p9::session(std::unique_ptr<p9::transport>(f), ceiling, slot));
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
    f->probe = s.get();
    caller c(*s, p9::Tread, read_body(root), p9::op_deadline());
    require(f->await(1), "request submitted");
    f->prober.join();
    require(f->probe_ok, "session lock not held across submit");
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

// Wait (bounded) for the flush; return it.
static std::shared_ptr<request> flush_of(fake* f, unsigned n,
                                         const request& orig)
{
    require(f->await(n), "Tflush submitted");
    auto fl = f->at(f->pending() - 1);
    frame t = decoded(*fl);
    require(t.type == p9::Tflush && t.tag == fl->tag && t.body.size() == 2
            && uint16_t(t.body[0] | t.body[1] << 8) == orig.tag,
            "Tflush(oldtag) on its own tag");
    require(fl->tag != orig.tag && fl->tag != p9::notag, "flush tag distinct");
    return fl;
}

static frame rflush(uint16_t tag)
{
    return frame{p9::Rflush, tag, {}};
}

static void flush_then_rflush()
{
    fake* f;
    auto s = make(f);
    unsigned n0 = s->completions();
    caller c(*s, p9::Tread, read_body(root), after(milliseconds(100)));
    require(f->await(1), "request submitted");
    auto r = f->at(0);
    auto fl = flush_of(f, 2, *r);
    require(!c.returns_within(milliseconds(50)),
            "caller waits for Rflush");
    f->reply(*fl, rflush(fl->tag));
    require(c.returns_within(milliseconds(2000)), "Rflush wakes caller");
    c.join();
    require(c.rc == ETIMEDOUT && !s->dead(), "canceled, session healthy");
    require(s->busy() == 2, "both buffers still owned by the transport");
    // Rflush retired the old tag but not its buffer: write into it now.
    std::fill(r->rx.begin(), r->rx.end(), 0xaa);
    f->retire(fl);
    require(s->busy() == 1, "flush slot freed by its own retirement");
    f->retire(r);
    require(s->busy() == 0 && s->charged() == 0 && s->completions() == n0 + 1,
            "retired once, completed once");
    std::puts("PASS: flush then Rflush");
}

static void reply_wins_flush()
{
    fake* f;
    auto s = make(f);
    unsigned n0 = s->completions();
    caller c(*s, p9::Tread, read_body(root), after(milliseconds(100)));
    require(f->await(1), "request submitted");
    auto r = f->at(0);
    auto fl = flush_of(f, 2, *r);
    f->reply(*r, rread(r->tag));
    require(!c.returns_within(milliseconds(50)),
            "caller still waits for Rflush after the original");
    // The old tag stays held until Rflush: a new request cannot reuse it.
    f->autoreply = false;
    caller d(*s, p9::Tread, read_body(root), p9::op_deadline());
    require(f->await(3), "another request admitted");
    require(f->at(2)->tag != r->tag, "oldtag not reissued before Rflush");
    f->reply(*fl, rflush(fl->tag));
    c.join();
    require(c.rc == 0 && c.reply.type == p9::Rread,
            "original reply honored once");
    auto q = f->at(2);
    f->reply(*q, rread(q->tag));
    d.join();
    f->retire(r);
    f->retire(fl);
    f->retire(q);
    require(!s->dead() && s->busy() == 0 && s->completions() == n0 + 2,
            "one completion each");
    std::puts("PASS: reply wins flush");
}

static void flush_timeout_kills()
{
    fake* f;
    auto s = make(f);
    auto t0 = osv::clock::uptime::now();
    caller c(*s, p9::Tread, read_body(root), after(milliseconds(50)));
    require(f->await(2), "request and flush submitted");
    c.join();
    auto took = osv::clock::uptime::now() - t0;
    require(c.rc == ETIMEDOUT && s->dead() == ETIMEDOUT,
            "no Rflush in 5 s kills the session");
    require(took >= p9::flush_timeout && took < p9::flush_timeout
            + seconds(3), "flush deadline is 5 s");
    require(s->busy() == 2, "buffers still owned until retired");
    f->shutdown(p9::op_deadline());
    require(s->busy() == 0 && s->charged() == 0, "retired after death");
    std::puts("PASS: flush timeout");
}

static void abort_cancels()
{
    fake* f;
    auto s = make(f);
    caller c(*s, p9::Tread, read_body(root), p9::op_deadline());
    require(f->await(1), "request submitted");
    auto r = f->at(0);
    s->abort(EINTR);
    auto fl = flush_of(f, 2, *r);
    f->reply(*fl, rflush(fl->tag));
    c.join();
    require(c.rc == EINTR, "abort errno returned");
    f->retire(r);
    f->retire(fl);
    frame out;
    f->autoreply = true;
    require(s->rpc(p9::Tread, read_body(root), out, p9::op_deadline()) == 0,
            "later requests unaffected");
    std::puts("PASS: abort");
}

static void serialized_flushes()
{
    // Two requests time out together; only one Tflush may be outstanding.
    fake* f;
    auto s = make(f);
    caller a(*s, p9::Tread, read_body(root), after(milliseconds(50)));
    caller b(*s, p9::Tread, read_body(root), after(milliseconds(50)));
    require(f->await(3), "two requests and one flush");
    std::this_thread::sleep_for(milliseconds(100));
    require(f->submitted == 3, "second flush waits for the slot");
    auto fl = f->at(2);
    f->reply(*fl, rflush(fl->tag));
    std::this_thread::sleep_for(milliseconds(50));
    require(f->submitted == 3, "flush slot held until its buffer retires");
    f->retire(fl);
    require(f->await(4), "second flush once the first fully retires");
    auto fl2 = f->at(f->pending() - 1);
    require(decoded(*fl2).type == p9::Tflush, "second Tflush");
    f->reply(*fl2, rflush(fl2->tag));
    a.join();
    b.join();
    require(a.rc == ETIMEDOUT && b.rc == ETIMEDOUT && !s->dead(),
            "both canceled");
    f->shutdown(p9::op_deadline());
    require(s->busy() == 0, "drained");
    std::puts("PASS: serialized flushes");
}

static void cancel_racing_create()
{
    // A walk canceled after the server created newfid: the fid is clunked.
    fake* f;
    auto s = make(f);
    unsigned before = s->fids();
    uint32_t nf = 0;
    bytes qids;
    caller c([&] {
        return s->walk(root, {"x"}, nf, qids, after(milliseconds(50)));
    });
    require(f->await(1), "walk submitted");
    auto r = f->at(0);
    auto fl = flush_of(f, 2, *r);
    f->reply(*r, f->answer(decoded(*r))); // Rwalk wins
    f->reply(*fl, rflush(fl->tag));
    require(f->await(3), "compensating Tclunk");
    auto cl = f->at(2);
    frame t = sent(*cl);
    require(t.type == p9::Tclunk && get32(t.body, 0) == get32(decoded(*r).body, 4),
            "clunk names the new fid");
    f->reply(*cl, frame{p9::Rclunk, cl->tag, {}});
    c.join();
    require(c.rc == ETIMEDOUT && s->fids() == before && !s->dead(),
            "canceled walk leaves no fid behind");
    f->shutdown(p9::op_deadline());

    // Same for attach.
    auto s2 = make(f);
    before = s2->fids();
    caller d([&] {
        return s2->attach("u", "", 0, nf, qids, after(milliseconds(50)));
    });
    require(f->await(1), "attach submitted");
    r = f->at(0);
    fl = flush_of(f, 2, *r);
    f->reply(*r, f->answer(decoded(*r)));
    f->reply(*fl, rflush(fl->tag));
    require(f->await(3), "compensating Tclunk");
    cl = f->at(2);
    require(sent(*cl).type == p9::Tclunk, "Tclunk after attach");
    f->reply(*cl, frame{p9::Rclunk, cl->tag, {}});
    d.join();
    require(d.rc == ETIMEDOUT && s2->fids() == before && !s2->dead(),
            "canceled attach leaves no fid behind");
    f->shutdown(p9::op_deadline());

    // Rflush first: the fid was never created; nothing to clunk.
    auto s3 = make(f);
    before = s3->fids();
    caller e([&] {
        return s3->walk(root, {"x"}, nf, qids, after(milliseconds(50)));
    });
    require(f->await(1), "walk submitted");
    r = f->at(0);
    fl = flush_of(f, 2, *r);
    f->reply(*fl, rflush(fl->tag));
    e.join();
    require(e.rc == ETIMEDOUT && s3->fids() == before && f->submitted == 2,
            "flushed walk releases newfid without a clunk");
    f->shutdown(p9::op_deadline());
    std::puts("PASS: cancel racing create");
}

static void uncertain_clunk()
{
    fake* f;
    auto s = make(f);
    caller c([&] { return s->clunk(root, after(milliseconds(50))); });
    require(f->await(1), "clunk submitted");
    auto r = f->at(0);
    auto fl = flush_of(f, 2, *r);
    f->reply(*fl, rflush(fl->tag));
    c.join();
    require(s->dead() == EIO,
            "clunk canceled without a reply retires the session");
    require(c.rc == EIO, "and reports the session error, not ETIMEDOUT");
    require(s->fids() == 0, "fid dropped");
    f->shutdown(p9::op_deadline());

    // A clunk whose reply beats Rflush is certain: the session lives.
    auto s2 = make(f);
    caller d([&] { return s2->clunk(root, after(milliseconds(50))); });
    require(f->await(1), "clunk submitted");
    r = f->at(0);
    fl = flush_of(f, 2, *r);
    f->reply(*r, frame{p9::Rclunk, r->tag, {}});
    f->reply(*fl, rflush(fl->tag));
    d.join();
    require(d.rc == 0 && !s2->dead() && s2->fids() == 0, "certain clunk");
    f->shutdown(p9::op_deadline());
    std::puts("PASS: uncertain clunk");
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

static void transport_failure()
{
    fake* f;
    auto s = make(f);
    unsigned n0 = s->completions();
    std::deque<caller> cs;
    for (unsigned i = 0; i < p9::data_tags; i++) {
        cs.emplace_back(*s, p9::Tread, read_body(root), p9::op_deadline());
    }
    require(f->await(p9::data_tags), "32 in flight");
    // One canceled (its Tflush outstanding), one waiting for a slot.
    caller late(*s, p9::Tread, read_body(root), p9::op_deadline());
    s->abort(EINTR);
    require(f->await(p9::data_tags + 1), "a Tflush outstanding");
    std::this_thread::sleep_for(milliseconds(20));
    unsigned sent = f->submitted;
    f->failed_from_transport(*s, ECONNRESET);
    for (auto& c : cs) {
        require(c.returns_within(milliseconds(2000)), "waiter woken");
        c.join();
        require(c.rc == ECONNRESET, "with the transport error");
    }
    late.join();
    require(late.rc == EINTR || late.rc == ECONNRESET,
            "slot waiter woken without sending");
    require(s->completions() == n0 + p9::data_tags,
            "each admitted request completed exactly once");
    require(f->submitted == sent, "nothing resent");
    frame out;
    require(s->rpc(p9::Tread, read_body(root), out, p9::op_deadline())
            == ECONNRESET && f->submitted == sent, "no new work");
    require(s->busy() == p9::data_tags + 1 && s->charged() > 0,
            "buffers held until the transport retires them");
    // Late completions from the broken transport land in owned buffers.
    auto r = f->at(0);
    f->reply(*r, rread(r->tag));
    require(s->dead() == ECONNRESET, "late reply dropped");
    require(s->shutdown(p9::op_deadline()) == 0, "shutdown retires all");
    require(s->busy() == 0 && s->charged() == 0, "then releases");
    std::puts("PASS: transport failure");
}

static void shutdown_waits_for_retirement()
{
    fake* f;
    auto s = make(f);
    caller c(*s, p9::Tread, read_body(root), p9::op_deadline());
    require(f->await(1), "submitted");
    auto r = f->at(0);
    f->stuck = true;
    require(s->shutdown(p9::op_deadline()) == EBUSY,
            "shutdown reports buffers still owned");
    c.join();
    require(c.rc == ESHUTDOWN, "waiter woken by shutdown");
    require(s->busy() == 1 && s->charged() > 0, "buffer not released");
    // The device still writes into the buffer: it must be alive.
    std::fill(r->rx.begin(), r->rx.end(), 0x55);
    f->stuck = false;
    require(s->shutdown(p9::op_deadline()) == 0, "second shutdown completes");
    require(s->busy() == 0 && s->charged() == 0, "released after retirement");
    frame out;
    require(s->rpc(p9::Tread, read_body(root), out, p9::op_deadline())
            == ESHUTDOWN, "no work after shutdown");
    uint32_t x;
    bytes q;
    require(s->negotiate(65536, p9::op_deadline()) == ESHUTDOWN
            && s->attach("u", "", 0, x, q, p9::op_deadline()) == ESHUTDOWN,
            "helpers refused too");

    // Destroying a session whose transport still holds a request: the
    // request outlives the session through the transport's share.
    fake* g;
    auto t = make(g);
    caller d(*t, p9::Tread, read_body(root), p9::op_deadline());
    require(g->await(1), "submitted");
    auto keep = g->at(0);
    g->stuck = true;
    t->shutdown(p9::op_deadline());
    d.join();
    require(d.rc == ESHUTDOWN, "woken");
    t.reset(); // deletes g with its share; ours keeps the buffer alive
    std::fill(keep->rx.begin(), keep->rx.end(), 0x66);
    require(keep.use_count() == 1, "only the test holds it now");
    std::puts("PASS: shutdown");
}

static void stress()
{
    // Callers, a replying server thread, timeouts and a transport failure
    // racing on real threads. Every caller returns once; nothing leaks.
    for (unsigned round = 0; round < 20; round++) {
        fake* f;
        auto s = make(f);
        unsigned n0 = s->completions();
        std::atomic<bool> stop{false};
        std::thread server([&] {
            unsigned k = 0;
            while (!stop) {
                // Answer a request picked out of order, then retire it.
                std::shared_ptr<request> q;
                WITH_LOCK(f->mtx) {
                    if (!f->held.empty()) {
                        q = f->held[k++ % f->held.size()];
                    }
                }
                if (!q) {
                    std::this_thread::yield();
                    continue;
                }
                frame t = decoded(*q);
                if (t.type == p9::Tflush) {
                    // flush(5): the original, if unanswered, gets no reply.
                    uint16_t old = uint16_t(t.body[0] | t.body[1] << 8);
                    std::shared_ptr<request> o;
                    WITH_LOCK(f->mtx) {
                        for (auto& h : f->held) {
                            if (h->tag == old && h != q) {
                                o = h;
                            }
                        }
                    }
                    if (o) {
                        f->retire(o);
                    }
                    f->reply(*q, rflush(t.tag));
                } else {
                    f->reply(*q, rread(t.tag));
                }
                f->retire(q);
            }
        });
        std::deque<caller> cs;
        for (unsigned i = 0; i < 48; i++) {
            cs.emplace_back(*s, p9::Tread, read_body(root),
                            after(milliseconds(i % 3 ? 1000 : 1)));
        }
        std::this_thread::sleep_for(milliseconds(round % 5));
        if (round % 2) {
            f->failed_from_transport(*s, ECONNRESET);
        }
        unsigned ok = 0;
        for (auto& c : cs) {
            c.join();
            require(c.rc == 0 || c.rc == ETIMEDOUT || c.rc == ECONNRESET,
                    "bounded result");
            ok += c.rc == 0;
        }
        stop = true;
        server.join();
        require(s->completions() - n0 <= 48 && s->completions() - n0 >= ok,
                "at most one completion per caller");
        require(s->shutdown(p9::op_deadline()) == 0 && s->busy() == 0
                && s->charged() == 0, "drained");
    }
    std::puts("PASS: stress");
}

// Errno values the session itself uses (EINPROGRESS, EAGAIN, ENOMEM) can
// also come from the server or the transport; none may be read as state.
static void errno_values()
{
    // Rlerror(115) to one of two concurrent callers: each gets its own.
    fake* f;
    auto s = make(f);
    caller a(*s, p9::Tread, read_body(root), p9::op_deadline());
    require(f->await(1), "first submitted");
    caller b(*s, p9::Tread, read_body(root), p9::op_deadline());
    require(f->await(2), "second submitted");
    auto ra = f->at(0), rb = f->at(1);
    f->reply(*ra, frame{p9::Rlerror, ra->tag, {115, 0, 0, 0}});
    f->retire(ra);
    require(a.returns_within(milliseconds(2000)), "Rlerror(115) returns");
    a.join();
    require(a.rc == EINPROGRESS && a.reply.type == p9::Rlerror
            && a.reply.tag == ra->tag, "Rlerror(115) is the first caller's");
    caller c(*s, p9::Tread, read_body(root), p9::op_deadline());
    require(f->await(3), "third reuses the freed slot");
    auto rc = f->at(1);
    f->reply(*rc, rread(rc->tag));
    f->reply(*rb, rread(rb->tag));
    f->retire(rb);
    f->retire(rc);
    b.join();
    c.join();
    require(b.rc == 0 && b.reply.tag == rb->tag && c.rc == 0
            && c.reply.tag == rc->tag, "the others get their own replies");
    require(!s->dead() && s->busy() == 0, "session healthy");

    // Every ecode, answered inside submit(): returned once, mapped.
    f->autoreply = true;
    for (uint32_t e = 1; e <= 140; e++) {
        f->lerror = e;
        frame out;
        int got = s->rpc(p9::Tread, read_body(root), out,
                         after(milliseconds(2000)));
        require(got == p9::map_lerror(e) && out.type == p9::Rlerror,
                "Rlerror ecode returned as the result");
    }
    f->lerror = 0;
    f->autoreply = false;
    require(!s->dead() && s->busy() == 0, "session healthy");

    // A transport errno of EINPROGRESS fails negotiate instead of spinning.
    for (int how = 0; how < 2; how++) {
        fake* g = new fake;
        p9::session u{std::unique_ptr<p9::transport>(g)};
        g->fail_submit = how ? 0 : EINPROGRESS;
        caller n([&] { return u.negotiate(65536, after(milliseconds(200))); });
        if (how) {
            require(g->await(1), "Tversion submitted");
            g->failed_from_transport(u, EINPROGRESS);
        }
        require(n.returns_within(milliseconds(3000)),
                "negotiate returns on an EINPROGRESS transport error");
        n.join();
        require(n.rc == EINPROGRESS && u.dead() == EINPROGRESS,
                "with that error");
        g->shutdown(p9::op_deadline());
    }

    // The same errnos on a data request, and EAGAIN as the session error.
    int errs[] = {EINPROGRESS, EAGAIN, ENOMEM};
    for (int how = 0; how < 2; how++) {
        for (int e : errs) {
            auto t = make(f);
            f->fail_submit = how ? 0 : e;
            caller d(*t, p9::Tread, read_body(root), after(milliseconds(200)));
            if (how) {
                require(f->await(1), "submitted");
                f->failed_from_transport(*t, e);
            }
            require(d.returns_within(milliseconds(3000)), "rpc returns");
            d.join();
            require(d.rc == e && t->dead() == e, "with the transport errno");
            frame out;
            require(t->rpc(p9::Tread, read_body(root), out,
                           after(milliseconds(200))) == e,
                    "later rpc refused at once with the session errno");
            f->shutdown(p9::op_deadline());
        }
    }
    std::puts("PASS: errno values");
}

// A reply whose tag names another outstanding request, delivered in this
// request's buffer, fails the session; neither caller takes it as theirs.
static void crossed_reply()
{
    fake* f;
    auto s = make(f);
    caller a(*s, p9::Tread, read_body(root), p9::op_deadline());
    require(f->await(1), "first submitted");
    caller b(*s, p9::Tread, read_body(root), p9::op_deadline());
    require(f->await(2), "second submitted");
    auto ra = f->at(0), rb = f->at(1);
    f->reply(*ra, rread(rb->tag));
    require(s->dead() == EPROTO, "reply in another request's buffer fails");
    a.join();
    b.join();
    require(a.rc == EPROTO && b.rc == EPROTO, "neither caller takes it");
    f->shutdown(p9::op_deadline());
    require(s->busy() == 0 && s->charged() == 0, "retired after failure");

    // The flush tag too: an Rflush delivered in the original's buffer.
    auto t = make(f);
    caller c(*t, p9::Tread, read_body(root), after(milliseconds(50)));
    require(f->await(1), "submitted");
    auto r = f->at(0);
    auto fl = flush_of(f, 2, *r);
    f->reply(*r, rflush(fl->tag));
    require(t->dead() == EPROTO, "Rflush in the original's buffer fails");
    c.join();
    require(c.rc == EPROTO, "caller sees the protocol error");
    f->shutdown(p9::op_deadline());
    std::puts("PASS: crossed reply");
}

// A walk canceled after the server created newfid leaves an orphan fid
// that only the compensating clunk owns: no other caller may pin it, and
// if the clunk cannot be sent the session fails rather than leak it.
static void orphan_clunk()
{
    // No budget for the clunk while the walk's buffer is unretired, then
    // abort(): the clunk is never sent.
    fake* f;
    auto s = make(f, 7 + 17 + p9::min_msize); // one request at a time
    unsigned before = s->fids();
    uint32_t nf = 0;
    bytes qids;
    caller c([&] {
        return s->walk(root, {"x"}, nf, qids, after(milliseconds(50)));
    });
    require(f->await(1), "walk submitted");
    auto r = f->at(0);
    uint32_t orphan = get32(decoded(*r).body, 4);
    auto fl = flush_of(f, 2, *r);
    f->reply(*r, f->answer(decoded(*r)));
    f->reply(*fl, rflush(fl->tag));
    std::this_thread::sleep_for(milliseconds(100));
    require(f->submitted == 2 && !c.done, "compensating clunk waits");
    frame out;
    require(s->rpc(p9::Tread, read_body(orphan), out, after(milliseconds(1)))
            == EBADF && s->clunk(orphan, after(milliseconds(1))) == EBADF,
            "orphan not usable while its clunk waits");
    s->abort(EINTR);
    require(c.returns_within(milliseconds(2000)), "walk returns");
    c.join();
    require(c.rc == ETIMEDOUT, "walk returns its cancellation error");
    require(s->dead() == EIO, "unsent compensating clunk fails the session");
    require(s->fids() == before, "and the orphan is not left live");
    f->shutdown(p9::op_deadline());

    // Callers hammering the orphan's number while the walk resolves never
    // pin it (racy by nature: a hit is a failure, a miss proves nothing).
    unsigned hits = 0;
    for (unsigned trial = 0; trial < 200 && !hits; trial++) {
        auto t = make(f);
        before = t->fids();
        caller w([&] {
            return t->walk(root, {"x"}, nf, qids, after(milliseconds(2)));
        });
        require(f->await(1), "walk submitted");
        r = f->at(0);
        orphan = get32(decoded(*r).body, 4);
        std::atomic<bool> stop{false};
        std::atomic<unsigned> got{0};
        std::vector<std::thread> hammer;
        for (int i = 0; i < 3; i++) {
            hammer.emplace_back([&] {
                frame o;
                while (!stop) {
                    if (t->rpc(p9::Tread, read_body(orphan), o,
                               p9::op_deadline()) != EBADF) {
                        ++got;
                    }
                }
            });
        }
        fl = flush_of(f, 2, *r);
        f->autoreply = true; // the compensating clunk, and any slip
        f->reply(*r, f->answer(decoded(*r)));
        f->reply(*fl, rflush(fl->tag));
        w.join();
        stop = true;
        for (auto& h : hammer) {
            h.join();
        }
        hits += got;
        require(w.rc == ETIMEDOUT && !t->dead(), "canceled walk");
        require(got || t->fids() == before, "orphan clunked");
        f->autoreply = false;
        f->shutdown(p9::op_deadline());
    }
    require(hits == 0, "orphan fid never pinned by another caller");
    std::puts("PASS: orphan clunk");
}

// Every data request times out at once against a server that answers each
// Tflush, slowly: the last canceler queues behind 31 flushes taking longer
// than flush_timeout in total, and the session must survive.
static void mass_timeout()
{
    fake* f;
    auto s = make(f);
    const auto per_flush = milliseconds(250); // 32 x 250 ms > 5 s
    std::atomic<bool> stop{false};
    std::atomic<unsigned> flushes{0};
    std::thread server([&] {
        while (!stop) {
            std::shared_ptr<request> q;
            WITH_LOCK(f->mtx) {
                for (auto& h : f->held) {
                    if (decoded(*h).type == p9::Tflush) {
                        q = h;
                    }
                }
            }
            if (!q) {
                std::this_thread::sleep_for(milliseconds(1));
                continue;
            }
            std::this_thread::sleep_for(per_flush);
            f->reply(*q, rflush(q->tag));
            f->retire(q);
            ++flushes;
        }
    });
    std::deque<caller> cs;
    for (unsigned i = 0; i < p9::data_tags; i++) {
        cs.emplace_back(*s, p9::Tread, read_body(root), after(milliseconds(50)));
    }
    auto t0 = osv::clock::uptime::now();
    for (auto& c : cs) {
        c.join();
        require(c.rc == ETIMEDOUT, "each waiter canceled");
    }
    auto took = osv::clock::uptime::now() - t0;
    stop = true;
    server.join();
    require(took > p9::flush_timeout, "flushes took longer than 5 s in all");
    require(flushes == p9::data_tags && !s->dead(),
            "slow but answering server keeps the session");
    f->shutdown(p9::op_deadline());
    require(s->busy() == 0 && s->charged() == 0, "drained");
    std::puts("PASS: mass timeout");
}

// A transport that delivers Rflush but never retires the Tflush buffer
// keeps the flush slot held. A later canceler waits for the slot only
// until the slot bound, then fails the session; its Tflush is never sent.
static void flush_slot_expiry()
{
    fake* f;
    const auto bound = milliseconds(300);
    auto s = make(f, p9::alloc_ceiling, true, bound);
    caller a(*s, p9::Tread, read_body(root), after(milliseconds(50)));
    require(f->await(1), "first submitted");
    auto fl = flush_of(f, 2, *f->at(0));
    f->reply(*fl, rflush(fl->tag)); // and never retire fl
    a.join();
    require(a.rc == ETIMEDOUT && !s->dead(), "first canceled by Rflush");
    auto d = after(milliseconds(50));
    caller b(*s, p9::Tread, read_body(root), d);
    require(f->await(3), "second submitted");
    require(!b.returns_within(milliseconds(150)) && !s->dead(),
            "second waits for the held flush slot");
    require(b.returns_within(milliseconds(3000)),
            "slot wait expires at the session's slot bound");
    b.join();
    auto took = osv::clock::uptime::now() - d;
    require(b.rc == ETIMEDOUT && s->dead() == ETIMEDOUT,
            "slot expiry fails the session");
    require(took >= bound, "not before the slot bound");
    require(f->submitted == 3, "second Tflush never sent");
    f->shutdown(p9::op_deadline());
    require(s->busy() == 0 && s->charged() == 0, "retired after failure");
    std::puts("PASS: flush slot expiry");
}

int main()
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0); // keep lines if killed
    negotiation();
    fids();
    plain_rpc();
    flush_then_rflush();
    reply_wins_flush();
    flush_timeout_kills();
    abort_cancels();
    serialized_flushes();
    cancel_racing_create();
    uncertain_clunk();
    admission();
    ceiling();
    contract();
    many();
    transport_failure();
    shutdown_waits_for_retirement();
    errno_values();
    crossed_reply();
    orphan_clunk();
    mass_timeout();
    flush_slot_expiry();
    stress();
    std::printf("SUMMARY: %u session checks, 0 failures\n", checks);
    return 0;
}
