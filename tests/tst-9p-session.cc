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

// Holds every submitted request until the test answers and retires it.
class fake : public p9::transport {
public:
    int fail_submit = 0;
    std::vector<std::shared_ptr<request>> held; // submitted, not retired
    std::atomic<unsigned> submitted{0};

    int submit(std::shared_ptr<request> r, p9::deadline) override
    {
        WITH_LOCK(mtx) {
            if (fail_submit) {
                return fail_submit;
            }
            held.push_back(r);
            ++submitted;
            cv.wake_all();
        }
        return 0;
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

static frame sent(const request& r)
{
    frame f;
    require(p9::decode(r.tx.data(), r.tx.size(), 65536, f) == 0,
            "tx decodes");
    require(f.tag == r.tag, "tx carries the admitted tag");
    return f;
}

static bytes clunk_body(uint32_t fid)
{
    return bytes{uint8_t(fid), uint8_t(fid >> 8), uint8_t(fid >> 16),
                 uint8_t(fid >> 24)};
}

static frame rclunk(uint16_t tag)
{
    return frame{p9::Rclunk, tag, {}};
}

// One rpc on its own thread; result read after join.
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

static std::unique_ptr<p9::session> make(fake*& f,
                                         size_t ceiling = p9::alloc_ceiling)
{
    f = new fake;
    return std::unique_ptr<p9::session>(
        new p9::session(std::unique_ptr<p9::transport>(f), ceiling));
}

static void plain_rpc()
{
    fake* f;
    auto s = make(f);
    caller c(*s, p9::Tclunk, clunk_body(7), p9::op_deadline());
    require(f->await(1), "request submitted");
    auto r = f->at(0);
    frame t = sent(*r);
    require(t.type == p9::Tclunk && t.body == clunk_body(7), "tx encoded");
    require(s->charged() > 0, "request charged while outstanding");
    f->reply(*r, rclunk(r->tag));
    require(c.returns_within(milliseconds(2000)),
            "reply wakes the caller before retirement");
    c.join();
    require(c.rc == 0 && c.reply.type == p9::Rclunk && c.reply.tag == r->tag,
            "reply returned to caller");
    require(s->busy() == 1 && s->charged() > 0,
            "slot and charge held until the transport retires the buffer");
    f->retire(r);
    require(s->busy() == 0 && s->charged() == 0, "retirement frees slot");
    require(r.use_count() == 1, "session dropped its request reference");

    caller e(*s, p9::Tclunk, clunk_body(8), p9::op_deadline());
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
    caller c(*s, p9::Tclunk, clunk_body(1), after(milliseconds(200)));
    require(f->await(1), "request submitted");
    c.join();
    require(c.rc == ETIMEDOUT, "deadline bounds the wait");
    require(s->dead() != 0, "uncertain request fails the session");
    auto r = f->at(0);
    require(s->busy() == 1, "request outlives its caller until retired");
    // The transport still owns the buffers: write a late reply into them.
    f->reply(*r, rclunk(r->tag));
    require(sent(*r).type == p9::Tclunk, "tx intact after caller returned");
    f->retire(r);
    require(s->busy() == 0 && s->charged() == 0, "late retirement frees");
    frame out;
    require(s->rpc(p9::Tclunk, clunk_body(2), out, p9::op_deadline())
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
        cs.emplace_back(*s, p9::Tclunk, clunk_body(i), p9::op_deadline());
    }
    require(f->await(p9::data_tags), "32 requests in flight");
    frame out;
    auto t0 = osv::clock::uptime::now();
    require(s->rpc(p9::Tclunk, clunk_body(99), out, after(milliseconds(200)))
                == ETIMEDOUT, "33rd request times out waiting for a slot");
    require(osv::clock::uptime::now() - t0 >= milliseconds(200),
            "33rd waited until its deadline");
    require(f->submitted == p9::data_tags && !s->dead(),
            "33rd sent nothing and the session is healthy");
    caller late(*s, p9::Tclunk, clunk_body(100), p9::op_deadline());
    std::this_thread::sleep_for(milliseconds(50));
    require(f->submitted == p9::data_tags, "waiter blocked while full");
    auto r = f->at(0);
    f->reply(*r, rclunk(r->tag));
    std::this_thread::sleep_for(milliseconds(50));
    require(f->submitted == p9::data_tags,
            "reply alone does not free a slot");
    f->retire(r);
    require(f->await(p9::data_tags + 1), "retirement admits the waiter");
    while (f->pending()) {
        r = f->at(0);
        f->reply(*r, rclunk(r->tag));
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
    // Room for exactly two requests' buffers: 7 + 4 + msize each.
    const size_t one = 7 + 4 + p9::min_msize;
    fake* f;
    auto s = make(f, 2 * one + one / 2);
    caller a(*s, p9::Tclunk, clunk_body(1), p9::op_deadline());
    caller b(*s, p9::Tclunk, clunk_body(2), p9::op_deadline());
    require(f->await(2), "two requests within the ceiling");
    require(s->charged() == 2 * one, "tx and rx both charged");
    caller c(*s, p9::Tclunk, clunk_body(3), p9::op_deadline());
    std::this_thread::sleep_for(milliseconds(100));
    require(f->submitted == 2, "third waits for budget");
    auto r = f->at(0);
    f->reply(*r, rclunk(r->tag));
    f->retire(r);
    require(f->await(3), "refund admits the third");
    while (f->pending()) {
        r = f->at(0);
        f->reply(*r, rclunk(r->tag));
        f->retire(r);
    }
    a.join();
    b.join();
    c.join();
    require(a.rc == 0 && b.rc == 0 && c.rc == 0, "all completed");
    frame out;
    fake* g;
    auto tiny = make(g, one - 1);
    require(tiny->rpc(p9::Tclunk, clunk_body(1), out, p9::op_deadline())
                == ENOMEM, "request larger than the ceiling refused");
    require(g->submitted == 0, "nothing sent");
    std::puts("PASS: ceiling");
}

static void contract()
{
    fake* f;
    auto s = make(f);
    caller c(*s, p9::Tclunk, clunk_body(1), p9::op_deadline());
    require(f->await(1), "submitted");
    auto r = f->at(0);
    f->reply(*r, rclunk(r->tag));
    c.join();
    require(c.rc == 0, "first reply honored");
    f->reply(*r, rclunk(r->tag));
    require(s->dead() == EPROTO, "second delivery on one request fails");
    f->retire(r);
    require(s->busy() == 0 && s->charged() == 0, "retired after failure");

    auto s2 = make(f);
    f->fail_submit = ECONNRESET;
    frame out;
    require(s2->rpc(p9::Tclunk, clunk_body(1), out, p9::op_deadline())
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
        caller c(*s, p9::Tclunk, clunk_body(i), p9::op_deadline());
        require(f->await(i + 1), "submitted");
        auto r = f->at(0);
        f->reply(*r, rclunk(r->tag));
        f->retire(r);
        c.join();
        require(c.rc == 0, "sequential rpc");
    }
    require(!s->dead() && s->busy() == 0, "no residue");
    std::puts("PASS: sequential");
}

int main()
{
    plain_rpc();
    reply_after_caller_left();
    admission();
    ceiling();
    contract();
    many();
    std::printf("SUMMARY: %u session checks, 0 failures\n", checks);
    return 0;
}
