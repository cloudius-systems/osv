/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A slow cleanup in the detached-thread reaper must not block other threads.
//
// The reaper runs each detached thread's cleanup after the thread ends.  A
// fork child's cleanup tears down its address space and fds, which can take
// milliseconds or sleep on I/O.  While one cleanup runs:
//   1. handing a thread to the reaper (detach() of a finished thread, or the
//      end of a detached thread) must not wait for that cleanup, and
//   2. the threads that end meanwhile must be reaped without waiting for it.
// Here one cleanup sleeps for 3 s; 1000 other threads are handed over during
// it, and both latencies must stay far below that.

#include <osv/sched.hh>
#include <osv/clock.hh>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cstdio>

using namespace std::chrono;
using clk = osv::clock::uptime;

static constexpr int nthreads = 1000;
static constexpr auto slow_cleanup = 3s;
static constexpr auto limit = 500ms;

static std::atomic<int> slow_state;   // 0 idle, 1 in the slow cleanup, 2 done

// A detached thread whose cleanup sleeps for slow_cleanup.  Returns once that
// cleanup has started.
static void start_slow_cleanup()
{
    slow_state = 0;
    auto *t = sched::thread::make([] {}, sched::thread::attr().detached());
    t->set_cleanup([t] {
        slow_state = 1;
        sched::thread::sleep(slow_cleanup);
        slow_state = 2;
        sched::thread::dispose(t);
    });
    t->start();
    while (slow_state != 1) {
        sched::thread::sleep(1ms);
    }
}

static void wait_slow_done()
{
    while (slow_state != 2) {
        sched::thread::sleep(10ms);
    }
}

static long ms(clk::duration d)
{
    return duration_cast<milliseconds>(d).count();
}

static int failures;

static void report(const char *what, std::vector<clk::duration> &v)
{
    std::sort(v.begin(), v.end());
    auto mx = v.back();
    bool ok = mx < limit;
    printf("%s: %s: p50 %ld us, max %ld ms over %zu threads (limit %ld ms)\n",
           ok ? "PASS" : "FAIL", what,
           (long)duration_cast<microseconds>(v[v.size() / 2]).count(),
           ms(mx), v.size(), ms(limit));
    if (!ok) {
        failures++;
    }
}

int main()
{
    // 1. detach() of a finished thread hands it to the reaper in the caller.
    std::vector<sched::thread *> done(nthreads);
    for (auto &t : done) {
        t = sched::thread::make([] {});
        t->start();
    }
    for (auto *t : done) {
        while (t->get_status() != sched::thread::status::terminated) {
            sched::thread::sleep(1ms);
        }
    }
    start_slow_cleanup();
    std::vector<clk::duration> handover;
    for (auto *t : done) {
        auto t0 = clk::now();
        t->detach();
        handover.push_back(clk::now() - t0);
    }
    report("detach() of a finished thread during a slow cleanup", handover);
    wait_slow_done();

    // 2. Threads that end during a slow cleanup are reaped meanwhile.
    std::vector<std::atomic<long>> ended(nthreads), reaped(nthreads);
    std::atomic<int> nreaped {0};
    start_slow_cleanup();
    for (int i = 0; i < nthreads; i++) {
        auto *t = sched::thread::make([&ended, i] {
            ended[i] = clk::now().time_since_epoch().count();
        }, sched::thread::attr().detached());
        t->set_cleanup([t, i, &reaped, &nreaped] {
            reaped[i] = clk::now().time_since_epoch().count();
            nreaped++;
            sched::thread::dispose(t);
        });
        t->start();
    }
    auto deadline = clk::now() + slow_cleanup + 10s;
    while (nreaped < nthreads && clk::now() < deadline) {
        sched::thread::sleep(10ms);
    }
    std::vector<clk::duration> lat;
    for (int i = 0; i < nthreads; i++) {
        lat.push_back(clk::duration(reaped[i] - ended[i]));
    }
    if (nreaped < nthreads) {
        printf("FAIL: only %d of %d threads reaped\n", nreaped.load(), nthreads);
        failures++;
    } else {
        report("end of a thread to its cleanup, during a slow cleanup", lat);
    }
    wait_slow_done();

    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
