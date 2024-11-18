/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Benchmark the effectiveness and fairness of the scheduler on a single CPU.
// Other benchmarks which involve multiple CPUs and moving threads between
// them can be found in tst-loadbalance.cc.
// NOTE: This test must be run with 1 cpu - it will refuse to run with more.
//
// This test checks the following scenarios:
//
// 1. Run several tight loops (wishing to use 100% of the CPU) together,
//    to see they each get a fair share of the CPU time.
// 2. Similarly, for several threads with different priorities.
// 3. Run a tight loop (wishing to use 100% of the CPU) together with an
//    intermittent thread which needs only 50% of the CPU (it loops for T
//    milliseconds, and then sleeps for T milliseconds, and so on).
//    We expect to see that each receives the same share - 50% - of the CPU,
//    i.e., both threads finish together after x2 the time of the tight loop
//    would take alone.

#include <thread>
#include <chrono>
#include <iostream>
#include <vector>

#ifdef __OSV__
#include <osv/sched.hh>
#include <osv/mutex.h>
#endif

void _loop(int iterations)
{
    for (register int i=0; i<iterations; i++) {
        for (register int j=0; j<10000; j++) {
            // To force gcc to not optimize this loop away
            asm volatile("" : : : "memory");
        }
    }
}

double loop(int iterations)
{
    auto start = std::chrono::system_clock::now();
    _loop(iterations);
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> sec = end - start;
    return sec.count();
}

// Runs "times" times, each time loops for "iterations" iterations and then
// sleeps "sleepms" milliseconds.
// Returns number of seconds (floating-point) the test took.
double intermittent(int times, int iterations, int sleepms)
{
    std::cout << "starting intermittent thread: " << times << " times, each with " << iterations << " iterations and "<< sleepms << "ms sleep\n";
    auto start = std::chrono::system_clock::now();
    for (int i = 0; i < times; i++) {
        _loop(iterations);
        std::this_thread::sleep_for(
                std::chrono::milliseconds(sleepms));
    }
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> sec = end - start;
    return sec.count();
}


void concurrent_loops(int looplen, int Nloop, int Nintermittent,
        int intermittent_sleepms, int intermittent_looplen,
        double secs, double expect)
{
    std::cout << "\nRunning concurrently " << Nloop << " tight loops and " <<
            Nintermittent << " intermittent loops. Expecting x" <<
            expect << ".\n";
    auto start = std::chrono::system_clock::now();
    std::vector<std::thread> threads;
    for (int i = 0; i < Nloop; i++) {
            threads.push_back(std::thread([=]() {
                double d = loop(looplen);
                std::cout << "tight thread " << i << ": " << d << " [x" << (d/secs) << "]\n";
            }));
    }
    for (int i = 0; i < Nintermittent; i++) {
            threads.push_back(std::thread([=]() {
                double d = intermittent(looplen/intermittent_looplen,
                        intermittent_looplen, intermittent_sleepms);
                std::cout << "intermittent thread " << i << ": " << d << " [x" << (d/secs) << "]\n";
            }));
    }
    for (auto &t : threads) {
        t.join();
    }
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> sec = end - start;
    double d = sec.count();
    std::cout << "all done in " << d << " [x" << (d/secs) << "]\n";
}

#ifdef __OSV__
void priority_test(std::vector<float> ps)
{
    std::cerr << "Starting priority test\n";
    std::vector<std::thread> threads;
    std::atomic<bool> done {false};
    std::vector<std::pair<float, int>> results;
    mutex mtx;
    for (auto p : ps) {
        threads.push_back(std::thread([&done, &mtx, &results, p]() {
            sched::thread::current()->set_priority(p);
            int it = 0;
            while (!done) {
                _loop(1);
                it++;
            }
            WITH_LOCK (mtx) {
                results.emplace_back(p, it);
            }
        }));
    }
    sleep(10);
    done = true;
    for (auto &t : threads) {
        t.join();
    }
    int minit = 1<<30;
    for (auto x : results) {
        if (x.second < minit) {
            minit = x.second;
        }
    }
    for (auto x : results) {
        std::cerr << x.first << ": " << x.second << " (x" << ((float)x.second/minit) << ")\n";
    }
    std::cerr << "Priority test done\n";
}
#endif

#ifdef __OSV__
void realtime_test(std::vector<int> ps)
{
    std::cerr << "Starting realtime test\n";
    std::vector<std::thread> threads;
    mutex mtx;
    for (auto p : ps) {
        threads.push_back(std::thread([p]() {
            sched::thread::current()->set_realtime_priority(p);
            std::cout << "Starting thread with realtime priority " << p << "\n";
            // Sleep a bit, to let all test threads get started. The thread
            // starting the test threads is not realtime, so it can be preempted
            // by the test threads.
            sleep(1);
            for (int i=0; i<10; i++) {
                _loop(100000);
                std::cout << p << std::flush;
            }
        }));
    }
    for (auto &t : threads) {
        t.join();
    }
    std::cerr << "\nRealtime test done\n";
}

void realtime_test2(bool yield)
{
    std::cerr << "Starting realtime test #2 - FIFO order, yield=" << yield << "\n";
    std::vector<std::thread> threads;
    mutex mtx;
    std::atomic<int> last_seen(-1);
    for (int p = 0; p < 10; p++) {
        threads.push_back(std::thread([p,yield,&last_seen]() {
            sched::thread::current()->set_realtime_priority(1);
            // Sleep a bit, to let all test threads get started. The thread
            // starting the test threads is not realtime, so it can be preempted
            // by the test threads.
            sleep(1);
            for (int i = 0 ; i < 3; i++) {
                for(int j = 0; j < 100000; j++) {
                    if (last_seen.exchange(p) != p) {
                        std::cout << p << std::flush; // context-switched to p
                    }
                    _loop(1);
                }
                if (yield)
                    sched::thread::yield();
                else
                    sched::thread::sleep(std::chrono::milliseconds(1));
            }
        }));
    }
    for (auto &t : threads) {
        t.join();
    }
    std::cerr << "\nRealtime test #2 done\n";
}
#endif


int main()
{
    if (std::thread::hardware_concurrency() != 1) {
        std::cerr << "Detected " << std::thread::hardware_concurrency() <<
                " CPUs, but this test requires exactly 1.\n";
        return 0;
    }

 #ifdef __OSV__
    // Tests for thread::set_realtime() support for POSIX-like realtime
    // scheduling.
    // TODO: Move this code into a real test, and in addition to just
    // printing progress, also save it into a string and check this string.
    // (Need to check the first 10 characters of this string repeat 2 more
    // times and that's it).
    realtime_test({0, 1, 2});
    realtime_test2(false);
    realtime_test2(true);
    // Check that intermittent thread with priority 2 doesn't force
    // realtime_test2 to context-switch more often than it normally
    // should (each time we the priority-2 thread sleeps, we need to
    // go back to the same priority-1 thread that previously ran - not
    // to the next one). We expect the output from the test below to
    // be identical to that from the test above.
    std::cout << "Additional intermittent thread with priority 2\n";
    std::atomic<bool> stop(false);
    std::thread ti([&stop]() {
        sched::thread::current()->set_realtime_priority(2);
        while (!stop.load()) {
            sched::thread::sleep(std::chrono::milliseconds(5));
        }
    });
    realtime_test2(true);
    stop.store(true);
    ti.join();
#endif

#ifdef __OSV__
    auto p = sched::thread::priority_default;
    priority_test({p, p*4});
    priority_test({p, p*2});
    priority_test({p, p});
    priority_test({p, p, p, p});
    priority_test({p, p, p/2, p*2});
#endif

    // Set secs to the desired number of seconds a measurement should
    // take. Note that the whole test will take several times longer than
    // secs, as we do several tests each lasting at least this long.
    double secs = 10.0;

    // Find looplen such that loop(looplen) takes "secs" seconds.
    // We first find how many iterations are needed for one second,
    // and then calculate looplen accordingly.
    int looplen;
    std::cout << "Calibrating loop length";
    std::cout.flush();
    double s;
    for(looplen=4096, s=0 ; s < 1.0 ; s = loop(looplen)){
        std::cout << ".";
        std::cout.flush();
        looplen *= 2;
    }
    looplen *= secs/s;
    std::cout << " chose " << looplen << " iterations, taking about "
            << secs << " seconds.\n";

    std::cout << "Running full loop... ";
    std::cout.flush();
    secs = loop(looplen);
    std::cout << secs << ". We'll call this \"x1\".\n\n";

    // Run the loop again to see the variance of the measurement
    std::cout << "Running loop again, expecting x1... ";
    std::cout.flush();
    double d = loop(looplen);
    std::cout << secs << " [x" << (d/secs) << "].\n\n";

    // Run N loops concurrently. If the scheduler is fair, we expect the
    // time to run each is the same - N times the time it took to run one.
    // to run this to be the same as the time to run one loop.
    concurrent_loops(looplen, 2, 0,0,0, secs, 2.0);
    concurrent_loops(looplen, 3, 0,0,0, secs, 3.0);

    // Estimate the loop length required for taking 1ms.
    int looplen_1ms = looplen / secs / 1000;
    std::cout << "\nRoughly 1ms loop: " << loop(looplen_1ms) << "\n";

    // Similarly, with intermittent threads with various durations.
    // Ideally, it will all be fair.
    concurrent_loops(looplen, 1, 1,1/*ms*/,1*looplen_1ms,  secs, 2.0);
    concurrent_loops(looplen, 1, 1,2/*ms*/,2*looplen_1ms,  secs, 2.0);
    concurrent_loops(looplen, 1, 1,4/*ms*/,4*looplen_1ms,  secs, 2.0);
    concurrent_loops(looplen, 1, 1,8/*ms*/,8*looplen_1ms,  secs, 2.0);
    concurrent_loops(looplen, 1, 1,16/*ms*/,16*looplen_1ms,  secs, 2.0);
    concurrent_loops(looplen, 1, 1,32/*ms*/,32*looplen_1ms,  secs, 2.0);


    return 0;
}
