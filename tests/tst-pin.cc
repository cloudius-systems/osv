/*
 * Copyright (C) 2016 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <unistd.h>
#include <errno.h>

#include <iostream>

#include <osv/sched.hh>
#include <osv/condvar.h>

static int tests = 0, fails = 0;

#define expect(actual, expected) do_expect(actual, expected, #actual, #expected, __FILE__, __LINE__)
template<typename T>
bool do_expect(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual != expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals <<
                ", expected " << expecteds << ", saw " << actual << ".\n";
        return false;
    }
    return true;
}

#define expect_errno_l(call, experrno) ( \
        do_expect(call, -1L, #call, "-1", __FILE__, __LINE__) && \
        do_expect(errno, experrno, #call " errno",  #experrno, __FILE__, __LINE__) )

int main(int argc, char **argv)
{
    if (sched::cpus.size() < 2) {
        // This test cannot be run on only one CPU, because we want to see
        // the effect of pinning threads to different CPUs.
        std::cout << "Cannot run this test with only one CPU.\n";
        return 0;
    }
    // pin this thread to a specific CPU and check the current CPU is
    // as expected (note that pin() only returns after the migration
    // completed).
    sched::thread::pin(sched::cpus[0]);
    expect(sched::cpu::current(), sched::cpus[0]);
    // re-pinning of this thread is allowed:
    sched::thread::pin(sched::cpus[1]);
    expect(sched::cpu::current(), sched::cpus[1]);
    sched::thread::pin(sched::cpus[0]);
    expect(sched::cpu::current(), sched::cpus[0]);
    // check that we can unpin this thread
    expect(sched::thread::current()->pinned(), true);
    sched::thread::current()->unpin();
    expect(sched::thread::current()->pinned(), false);

    // Check that we can pin a different thread.
    // In this test the thread will most likely be sleeping.
    mutex m;
    condvar c;
    bool t_pinned = false;
    std::unique_ptr<sched::thread> t(sched::thread::make([&] {
        WITH_LOCK (m) {
            while(!t_pinned) {
                c.wait(m);
            }
        }
        expect(sched::cpu::current(), sched::cpus[1]);
    }));
    t->start();
    sched::thread::pin(&*t, sched::cpus[1]);
    WITH_LOCK (m) {
        t_pinned = true;
        c.wake_all();
    }
    t->join();


    // Similar test for pinning a different thread, but in this
    // this test the thread will most likely be runnable, so we
    // we will verify a different code path.
    mutex m2;
    condvar c2;
    bool t2_pinned = false;
    std::unique_ptr<sched::thread> t2(sched::thread::make([&] {
        // Run in a tight loop to try to catch the case of trying to pin
        // a runnable thread
        auto now = osv::clock::uptime::now();
        while (osv::clock::uptime::now() < now + std::chrono::milliseconds(100)) {
            for (register int i = 0; i < 100000; i++) {
                // To force gcc to not optimize this loop away
                asm volatile("" : : : "memory");
            }
        }
        WITH_LOCK (m2) {
            while(!t2_pinned) {
                c.wait(m2);
            }
        }
        expect(sched::cpu::current(), sched::cpus[1]);
    }));
    t2->start();
    sched::thread::sleep(std::chrono::milliseconds(1));
    sched::thread::pin(&*t2, sched::cpus[1]);
    WITH_LOCK (m2) {
        t2_pinned = true;
        c2.wake_all();
    }
    t2->join();


    // Another similar test for pinning a different thread. In this
    // this test the thread is in a tight uptime() loop. uptime() very
    // frequently sets migrate_disable() temporarily - while getting a
    // per-cpu variable - so we will often (but not always!) exercise
    // here the code path of trying to migrate a non-migratable thread.
    mutex m3;
    condvar c3;
    bool t3_pinned = false;
    std::unique_ptr<sched::thread> t3(sched::thread::make([&] {
        auto now = osv::clock::uptime::now();
        while (osv::clock::uptime::now() < now + std::chrono::milliseconds(1000)) {
        }
        WITH_LOCK (m3) {
            while(!t3_pinned) {
                c.wait(m3);
            }
        }
        expect(sched::cpu::current(), sched::cpus[1]);
    }));
    t3->start();
    sched::thread::sleep(std::chrono::milliseconds(1));
    sched::thread::pin(&*t3, sched::cpus[1]);
    WITH_LOCK (m3) {
        t3_pinned = true;
        c3.wake_all();
    }
    t3->join();

    // Test a bug we had of pinning a thread which was already on the
    // given CPU. In that case, it stays there, but needs to become
    // pinned - we can't just ignore the call.
    mutex m4;
    condvar c4;
    bool t4_pinned = false;
    std::unique_ptr<sched::thread> t4(sched::thread::make([&] {
        WITH_LOCK (m4) {
            while(!t4_pinned) {
                c4.wait(m4);
            }
        }
    }));
    t4->start();
    sched::thread::sleep(std::chrono::milliseconds(1));
    expect(t4->migratable(), true);
    auto ccpu = t4->tcpu();
    sched::thread::pin(&*t4, ccpu);
    expect(t4->migratable(), false);
    expect(t4->tcpu(), ccpu);
    WITH_LOCK (m4) {
        t4_pinned = true;
        c4.wake_all();
    }
    t4->join();

    // Test pinning a thread several times in succession. It should work and
    // not hang (the second call shouldn't wait until the first pinning is
    // cancelled somehow).
    mutex m5;
    condvar c5;
    bool t5_pinned = false;
    std::unique_ptr<sched::thread> t5(sched::thread::make([&] {
        WITH_LOCK (m5) {
            while(!t5_pinned) {
                c5.wait(m5);
            }
            expect(sched::cpu::current(), sched::cpus[1]);
        }
    }));
    t5->start();
    sched::thread::sleep(std::chrono::milliseconds(1));
    sched::thread::pin(&*t5, sched::cpus[0]);
    sched::thread::pin(&*t5, sched::cpus[1]);
    sched::thread::pin(&*t5, sched::cpus[1]);
    sched::thread::pin(&*t5, sched::cpus[0]);
    sched::thread::pin(&*t5, sched::cpus[1]);
    expect(t5->migratable(), false);
    expect(t5->tcpu(), sched::cpus[1]);
    // also test unpin (only need to unpin once)
    expect(t5->pinned(), true);
    t5->unpin();
    expect(t5->pinned(), false);
    WITH_LOCK (m5) {
        t5_pinned = true;
        c5.wake_all();
    }
    t5->join();



    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
