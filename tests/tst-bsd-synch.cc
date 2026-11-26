/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Stress the BSD sleep/wakeup port (bsd/porting/synch.cc): msleep(),
// wakeup() and wakeup_one().  Checked semantics, as FreeBSD's sleep(9) and
// OSv's port define them:
//   - wakeup_one() wakes exactly one sleeper of the channel, in FIFO order,
//     and is a no-op on a channel with no sleeper;
//   - wakeup() wakes every sleeper of the channel and no other channel's;
//   - a timeout returns EWOULDBLOCK, and a timeout racing a wakeup returns
//     either 0 or EWOULDBLOCK but never loses or duplicates a wakeup_one;
//   - PDROP returns without the mutex, its absence returns with it;
//   - one channel may be slept on with different mutexes, and with none.
// Every test runs many channels concurrently so that channels sharing an
// internal bucket (if the implementation hashes them) are exercised.

#include <bsd/porting/netport.h>
#include <bsd/porting/sync_stub.h>
#include <bsd/porting/synch.h>
#include <bsd/sys/sys/param.h>

#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

#include <osv/mutex.h>

static int tests = 0, fails = 0;

static void report(bool ok, const char *msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", msg);
    fflush(stdout);
}

namespace {

// A channel is just an address; give each its own cache line and mutex.
struct alignas(64) chan {
    struct mtx m;
    std::atomic<int> woken{0};      // msleep returned 0
    std::atomic<int> timedout{0};   // msleep returned EWOULDBLOCK
    std::atomic<int> asleep{0};     // threads that entered msleep (approximate)
    int order_next = 0;             // FIFO ticket, under m
    std::vector<int> wake_order;    // ticket of each thread as it wakes, under m
};

void wait_until(std::function<bool()> pred, int ms = 5000)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!pred() && std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// Sleep on c (under c.m, so the sleep is registered before we can be woken)
// and record the outcome.  Returns msleep's result.
int sleep_on(chan &c, int timo, int prio, int *ticket = nullptr)
{
    mtx_lock(&c.m);
    int my = c.order_next++;
    if (ticket) {
        *ticket = my;
    }
    c.asleep++;
    int r = msleep(&c, &c.m, prio, "tst", timo);
    if (prio & PDROP) {
        mtx_lock(&c.m);
    }
    if (r == 0) {
        c.woken++;
        c.wake_order.push_back(my);
    } else {
        c.timedout++;
    }
    mtx_unlock(&c.m);
    return r;
}

// wakeup_one() wakes exactly one sleeper per call, in FIFO order, across
// many channels at once.
void test_wakeup_one_fifo()
{
    const int nchan = 64, per = 4;
    std::vector<chan> cs(nchan);
    for (auto &c : cs) {
        mtx_init(&c.m, "c", nullptr, MTX_DEF);
    }
    std::vector<std::thread> ts;
    for (int i = 0; i < nchan; i++) {
        for (int j = 0; j < per; j++) {
            // Start the sleepers of one channel one at a time so their FIFO
            // order is known.
            ts.emplace_back([&, i] { sleep_on(cs[i], 0, 0); });
            wait_until([&] { return cs[i].asleep.load() == j + 1; });
        }
    }
    bool ok = true;
    for (int k = 1; k <= per; k++) {
        for (auto &c : cs) {
            mtx_lock(&c.m);   // all sleepers are inside msleep once they drop it
            mtx_unlock(&c.m);
            wakeup_one(&c);
        }
        for (auto &c : cs) {
            wait_until([&] { return c.woken.load() >= k; });
            ok &= c.woken.load() == k;
        }
    }
    for (auto &t : ts) {
        t.join();
    }
    bool fifo = true;
    for (auto &c : cs) {
        for (int k = 0; k < per; k++) {
            fifo &= c.wake_order.size() == (size_t)per && c.wake_order[k] == k;
        }
        ok &= c.timedout.load() == 0;
    }
    report(ok, "wakeup_one wakes exactly one sleeper per call on 64 channels");
    report(fifo, "wakeup_one wakes the sleepers of a channel in FIFO order");
    // A wakeup_one on a channel nobody sleeps on is a no-op.
    wakeup_one(&cs[0]);
    report(cs[0].woken.load() == per, "wakeup_one on an empty channel is a no-op");
}

// wakeup() wakes all sleepers of its channel and nobody else.
void test_wakeup_all()
{
    const int nchan = 32, per = 6;
    std::vector<chan> cs(nchan);
    for (auto &c : cs) {
        mtx_init(&c.m, "c", nullptr, MTX_DEF);
    }
    std::vector<std::thread> ts;
    for (int i = 0; i < nchan; i++) {
        for (int j = 0; j < per; j++) {
            ts.emplace_back([&, i] { sleep_on(cs[i], 0, 0); });
        }
    }
    for (auto &c : cs) {
        wait_until([&] { return c.asleep.load() == per; });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    // Wake the even channels only; the odd ones must keep sleeping.
    for (int i = 0; i < nchan; i += 2) {
        mtx_lock(&cs[i].m);
        mtx_unlock(&cs[i].m);
        wakeup(&cs[i]);
    }
    bool ok = true;
    for (int i = 0; i < nchan; i += 2) {
        wait_until([&] { return cs[i].woken.load() == per; });
        ok &= cs[i].woken.load() == per;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bool others = true;
    for (int i = 1; i < nchan; i += 2) {
        others &= cs[i].woken.load() == 0;
    }
    report(ok, "wakeup wakes every sleeper of the channel");
    report(others, "wakeup does not wake another channel's sleepers");
    for (int i = 1; i < nchan; i += 2) {
        mtx_lock(&cs[i].m);
        mtx_unlock(&cs[i].m);
        wakeup(&cs[i]);
    }
    for (auto &t : ts) {
        t.join();
    }
}

// Timeouts: a sleep with nobody to wake it returns EWOULDBLOCK after about
// the timeout, and leaves nothing behind (a later wakeup_one is a no-op).
void test_timeout()
{
    chan c;
    mtx_init(&c.m, "c", nullptr, MTX_DEF);
    auto t0 = std::chrono::steady_clock::now();
    int r = sleep_on(c, 20, 0);   // 20 ticks at hz=1000 = 20 ms
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    report(r == EWOULDBLOCK, "an unwoken msleep with a timeout returns EWOULDBLOCK");
    report(ms >= 15 && ms < 2000, "the timeout takes about the requested time");
    // The timed-out waiter must be off the channel: if it were not, this
    // wakeup_one would "wake" a stack frame that no longer exists.
    wakeup_one(&c);
    wakeup(&c);
    report(true, "wakeup after a timed-out sleep does not touch the dead waiter");
}

// Timeouts racing wakeup_one: N sleepers with short timeouts on one channel
// and a waker calling wakeup_one M times.  Every wakeup_one that found a
// sleeper must be accounted for: woken == number of wakeup_one calls that
// had a sleeper to wake, which we cannot see directly, so check the
// invariant woken + timedout == sleeps and woken <= calls.
void test_timeout_race()
{
    const int nchan = 16, sleepers = 8, rounds = 200;
    std::vector<chan> cs(nchan);
    for (auto &c : cs) {
        mtx_init(&c.m, "c", nullptr, MTX_DEF);
    }
    std::atomic<bool> stop{false};
    std::atomic<long> calls{0};
    std::vector<std::thread> ts;
    for (int i = 0; i < nchan; i++) {
        for (int j = 0; j < sleepers; j++) {
            ts.emplace_back([&, i] {
                for (int k = 0; k < rounds; k++) {
                    sleep_on(cs[i], 1 + (k % 3), 0);
                }
            });
        }
    }
    std::thread waker([&] {
        while (!stop.load()) {
            for (auto &c : cs) {
                wakeup_one(&c);
                calls++;
            }
        }
    });
    for (auto &t : ts) {
        t.join();
    }
    stop = true;
    waker.join();
    bool ok = true;
    long woken = 0;
    for (auto &c : cs) {
        ok &= c.woken.load() + c.timedout.load() == sleepers * rounds;
        woken += c.woken.load();
    }
    report(ok, "every racing sleep returns exactly once (woken or timed out)");
    report(woken <= calls.load(), "no sleeper is woken more often than wakeup_one was called");
    printf("  race: %ld wakeup_one calls, %ld woken, %ld timed out\n", calls.load(), woken,
           (long)nchan * sleepers * rounds - woken);
}


// A timeout racing a wakeup_one must not lose the wakeup.  Sleeper A sleeps
// first with a 1-tick timeout, sleeper B second with none; one wakeup_one is
// sent at about A's deadline.  Either A is woken (returns 0) and B sleeps on,
// or A times out and the wakeup_one wakes B.  A lost wakeup is A returning
// EWOULDBLOCK while B is still asleep afterwards: the wakeup_one took A off
// the channel and A then reported a timeout.
void test_timeout_vs_wakeup_one()
{
    const int iters = 3000;
    int lost = 0, a_woken = 0, b_woken = 0;
    for (int i = 0; i < iters; i++) {
        chan c;
        mtx_init(&c.m, "c", nullptr, MTX_DEF);
        int ra = -1;
        std::thread a([&] { ra = sleep_on(c, 1, 0); });
        wait_until([&] { return c.asleep.load() == 1; });
        std::thread b([&] { sleep_on(c, 0, 0); });
        wait_until([&] { return c.asleep.load() == 2; });
        mtx_lock(&c.m);   // both are inside msleep once they released c.m
        mtx_unlock(&c.m);
        // Aim at A's deadline: spin for a little under a tick, varied.
        auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(700 + (i % 60) * 10);
        while (std::chrono::steady_clock::now() < until) {
        }
        wakeup_one(&c);
        a.join();
        // Give a woken B time to record itself.
        wait_until([&] { return c.woken.load() + c.timedout.load() >= 2; }, 20);
        bool b_done = c.woken.load() + c.timedout.load() >= 2;
        if (ra == 0) {
            a_woken++;
        } else if (b_done) {
            b_woken++;
        } else {
            lost++;
        }
        mtx_lock(&c.m);
        mtx_unlock(&c.m);
        wakeup(&c);   // release B
        b.join();
    }
    printf("  timeout vs wakeup_one: %d iterations, A woken %d, B woken %d, lost %d\n", iters,
           a_woken, b_woken, lost);
    report(lost == 0, "a timeout racing wakeup_one never loses the wakeup");
}

// PDROP: the mutex is released on return; without it the mutex is held.
void test_pdrop()
{
    chan c;
    mtx_init(&c.m, "c", nullptr, MTX_DEF);
    mtx_lock(&c.m);
    msleep(&c, &c.m, PDROP, "tst", 1);
    bool dropped = !mutex_owned(&c.m._mutex);
    if (!dropped) {
        mtx_unlock(&c.m);
    }
    mtx_lock(&c.m);
    msleep(&c, &c.m, 0, "tst", 1);
    bool held = mutex_owned(&c.m._mutex);
    mtx_unlock(&c.m);
    report(dropped, "PDROP returns without the mutex");
    report(held, "without PDROP msleep returns with the mutex");
}

// One channel, sleepers using two different mutexes and one using none
// (tsleep): one wakeup() wakes all three.
void test_mixed_mutexes()
{
    static char channel;
    struct mtx m1, m2;
    mtx_init(&m1, "m1", nullptr, MTX_DEF);
    mtx_init(&m2, "m2", nullptr, MTX_DEF);
    std::atomic<int> in{0}, out{0};
    std::thread a([&] { mtx_lock(&m1); in++; if (msleep(&channel, &m1, 0, "a", 5000) == 0) out++; mtx_unlock(&m1); });
    std::thread b([&] { mtx_lock(&m2); in++; if (msleep(&channel, &m2, 0, "b", 5000) == 0) out++; mtx_unlock(&m2); });
    std::thread n([&] { in++; if (tsleep(&channel, 0, "n", 5000) == 0) out++; });
    wait_until([&] { return in.load() == 3; });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    wakeup(&channel);
    a.join();
    b.join();
    n.join();
    report(out.load() == 3, "one wakeup wakes sleepers that used different mutexes and none");
}

// Churn: many short-lived channels (stack addresses), so that per-channel
// state the implementation keeps must be released when a channel empties.
void test_channel_churn()
{
    std::atomic<long> done{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; t++) {
        ts.emplace_back([&] {
            for (int k = 0; k < 20000; k++) {
                chan c;   // a new channel address every iteration
                mtx_init(&c.m, "c", nullptr, MTX_DEF);
                mtx_lock(&c.m);
                msleep(&c, &c.m, 0, "churn", 1);
                mtx_unlock(&c.m);
                wakeup(&c);
                done++;
            }
        });
    }
    for (auto &t : ts) {
        t.join();
    }
    report(done.load() == 8 * 20000, "160k sleeps on short-lived channels complete");
}

} // namespace

int main()
{
    test_wakeup_one_fifo();
    test_wakeup_all();
    test_timeout();
    test_timeout_race();
    test_timeout_vs_wakeup_one();
    test_pdrop();
    test_mixed_mutexes();
    test_channel_churn();
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
