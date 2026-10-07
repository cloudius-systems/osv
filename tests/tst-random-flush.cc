/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Checks that the random adaptor's reseed() flush waits for a harvest pass
// that starts after the flush is requested, so that entropy queued before the
// call is mixed in before the rekey.
//
// The harvest worker loops: drain the per-CPU rings, poll the live entropy
// sources, acknowledge flushes, sleep. This test registers a live source
// that contributes no bytes but can hold the worker inside its poll, that is,
// after the drain and before the acknowledgement. Flushes requested while the
// worker is held there must not be acknowledged by that pass, and must not
// return while the following pass is still running.
//
// Side effect: like any reseed() on OSv, this rekeys Yarrow and marks the
// device seeded. Test guests have virtio-rng, so it normally already is.

#include <osv/sched.hh>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include <dev/random/randomdev.h>
#include <dev/random/random_adaptors.h>
#include <dev/random/live_entropy_sources.h>

using namespace std::chrono;

// Polls of the test source are numbered from 1. The worker is single
// threaded, so poll n completes before poll n+1 starts.
static std::atomic<unsigned> entered{0}, completed{0}, allowed{0};

static int gate_read(void *, int)
{
    unsigned me = ++entered;
    while (allowed.load() < me) {
        sched::thread::sleep(milliseconds(1));
    }
    completed.store(me);
    return 0;
}

static random_hardware_source gate = {
    "tst-random-flush", RANDOM_PURE_RNDTEST, gate_read
};

[[noreturn]] static void fail(const char *why)
{
    printf("FAIL: tst-random-flush: %s\n", why);
    abort();
}

template <typename Pred>
static bool wait_for(Pred pred, milliseconds limit)
{
    auto end = steady_clock::now() + limit;
    while (!pred()) {
        if (steady_clock::now() > end) {
            return false;
        }
        sched::thread::sleep(milliseconds(1));
    }
    return true;
}

struct flusher {
    std::atomic<bool> started{false}, done{false};
    unsigned polls_started = 0;    // polls begun when the flush was called
    unsigned polls_completed = 0;  // polls finished when it returned
    sched::thread *t = nullptr;

    void run()
    {
        polls_started = entered.load();
        started.store(true);
        random_adaptor->reseed();
        polls_completed = completed.load();
        done.store(true);
    }

    // The flush must not return until a poll that began after it was
    // requested has completed, i.e. until a full later pass has run.
    void check()
    {
        if (polls_completed <= polls_started) {
            printf("FAIL: tst-random-flush: flush requested during poll %u "
                "returned after only %u poll(s) completed: it was "
                "acknowledged by the pass that was already past its drain\n",
                polls_started, polls_completed);
            abort();
        }
    }
};

int main()
{
    live_entropy_source_register(&gate);
    if (!wait_for([] { return entered.load() >= 1; }, seconds(5))) {
        fail("harvest worker never polled the test source");
    }

    // The worker is held in poll 1, after this pass's drain. Start two
    // flushers. Wait until each one sleeps: the first has published its
    // request, the second has published or is queued behind the first.
    flusher f[2];
    for (auto& x : f) {
        x.t = sched::thread::make([&x] { x.run(); },
            sched::thread::attr().name("tst-rnd-flush"));
        x.t->start();
        if (!wait_for([&] { return x.started.load() &&
                x.t->get_status() == sched::thread::status::waiting; },
                seconds(5))) {
            fail("flusher did not block");
        }
        sched::thread::sleep(milliseconds(50));
        if (x.done.load()) {
            fail("flush returned while the harvest worker was held");
        }
    }

    // Let the held pass finish and hold the next one in its poll. No flush
    // may return while that next pass is held.
    allowed.store(1);
    if (!wait_for([] { return entered.load() >= 2; }, seconds(5))) {
        fail("harvest worker did not start another pass");
    }
    sched::thread::sleep(seconds(1));
    for (auto& x : f) {
        if (x.done.load()) {
            x.check();
            fail("flush returned while a later pass was held");
        }
    }

    allowed.store(~0u);
    for (auto& x : f) {
        if (!wait_for([&] { return x.done.load(); }, seconds(5))) {
            fail("flush did not return");
        }
        x.check();
        x.t->join();
        delete x.t;
    }
    live_entropy_source_deregister(&gate);
    printf("PASS: tst-random-flush\n");
    return 0;
}
