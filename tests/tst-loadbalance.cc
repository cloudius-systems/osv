/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Test the effectiveness of the thread load-balancing feature of the scheduler
// NOTE: This test should be run with 2 cpus.
//
// The test begins by measuring a single-threaded tight loop that takes
// roughly 20 seconds. It then runs this loop in conjunction with other loads
// to see how effective the load balancing is. We check the following
// scenarios.
//
// 1. Run two concurrent loops on the 2 CPUs available. We expect to see the
//    loop time the same as the single-threaded time ("x1" in the output).
//
// 2. Run four concurrent loops, on the 2 CPUs available. We expect to see the
//    loop time double from single-thread time ("x2" in the output).
//
// 3. Two concurrent loops, plus one "intermittent thread" - a thread which
//    busy-loops for 1 millisecond, sleeps for 10 milliseconds, and so on
//    ad infinitum.
//    We expect fair a scheduler to let the intermittent thread run for 1ms
//    when it wants, so it uses 1/11 of one CPU, so with perfect load
//    balancing we expect a performance of (2-1/11)/2, i.e., the reported
//    loop measurement to be x1.05.
//
// 4. Four concurrent loops and the one intermittent thread. Again the
//    intermittent thread should take 1/11th of one CPU, and the expected
//    measurement is x2.1.
//
// Unexpected results in any of these tests should be debugged as follows:
//
// 1. Running "top" on the host during all these tests should show 200% CPU
//    use. Any less means that a CPU is being left idle while it could be
//    running one of the loops - and this can explain loops slower than
//    expected.
//
// 2. If the CPU use is at 200% but still the two or four loops are all
//    slower than expected, we might have inefficiency (e.g., too many
//    IPIs, slow context switches, etc.) that needs to be profiled.
//
// 3. If the run is unbalanced - different threads took different amount of
//    time - we have a problem with the fairness of our load balancer.
//
// 4. Other possible cause for a small slowdown and unbalance is the amount
//    of time it takes for the load balancer to act. When starting 4 threads
//    they all start on the same CPU and the load balancer might not migrate
//    them right away.

#include <thread>
#include <chrono>
#include <iostream>
#include <vector>

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

void concurrent_loops(int looplen, int N, double secs, double expect)
{
    std::cout << "\nRunning " << N << " concurrent loops. Expecting x" <<
            expect << ".\n";
    auto start = std::chrono::system_clock::now();
    std::vector<std::thread> threads;
    for (int i = 0; i < N; i++) {
            threads.push_back(std::thread([=]() {
                double d = loop(looplen);
                std::cout << "thread " << i << ": " << d << " [x" << (d/secs) << "]\n";
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

class background_intermittent {
public:
    void start(int looplen, int sleepms) {
        if (_t) {
            stop();
        }
        _stop = false;
        _t = new std::thread([=]() {
            while(!_stop) {
                _loop(looplen);
                std::this_thread::sleep_for(
                        std::chrono::milliseconds(sleepms));
            }
        });
    }
    void stop() {
        _stop = true;
    }
private:
    std::thread *_t = nullptr;
    bool _stop = false;
};

int main()
{
    // For expected values below, we assume running on 2 cpus.
    if (std::thread::hardware_concurrency() != 2) {
        std::cerr << "Detected " << std::thread::hardware_concurrency() <<
                " CPUs, but this test requires exactly 2.\n";
        return 0;
    }


    // Set secs to the desired number of seconds a measurement should
    // take. Note that the whole test will take several times longer than
    // secs, as we do several tests each lasting at least this long.
    double secs = 20.0;

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

    // Run N loops concurrently. If cpu load balancing is working correctly,
    // if N is lower than the number of (real) cores, we expect the time
    // to run this to be the same as the time to run one loop.
    concurrent_loops(looplen, 2, secs, 1.0);
    concurrent_loops(looplen, 4, secs, 2.0);

    std::cout << "\nStarting intermittent background thread:\n";
    // Estimate the loop length required for taking 1ms.
    int looplen_1ms = looplen / secs / 1000;
    std::cout << "Roughly 1ms loop: " << loop(looplen_1ms) << "\n";
    background_intermittent bi;

    bi.start(looplen_1ms, 10);
    concurrent_loops(looplen, 2, secs, 1.0*2/(2-1.0/11));
    concurrent_loops(looplen, 4, secs, 2.0*2/(2-1.0/11));
    bi.stop();

    return 0;
}
