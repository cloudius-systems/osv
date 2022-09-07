/*
 * Copyright (C) 2022 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <linux/futex.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <vector>

// This test is based on misc-mutex2.cc written by Nadav Har'El. But unlike
// the other one, it focuses on measuring the performance of the futex()
// syscall implementation. It does it indirectly by implementing mutex based
// on futex syscall according to the formula specified in the Ulrich Drepper's
// paper "Futexes Are Tricky".
// It takes three parameters: mandatory number of threads (nthreads) and
// a computation length (worklen) and optional number of mutexes (nmutexes).
// The test groups all threads (nthreads * nmutexes) into nmutexes sets
// where nthreads threads loop trying to take the group mutex (one out of nmutexes)
// and increment the group counter and then do some short computation of the
// specified length outside the loop. The test runs for 30 seconds, and
// shows the average number of lock-protected counter increments per second.
// The reason for doing some computation outside the lock is that makes the
// benchmark more realistic, reduces the level of contention and makes it
// beneficial for the OS to run the different threads on different CPUs:
// Without any computation outside the lock, the best performance will be
// achieved by running all the threads.

// Turn off optimization, as otherwise the compiler will optimize
// out calls to fmutex lock() and unlock() as they seem to do nothing
#pragma GCC optimize("00")

// Wrapper function that performs the same functionality as described
// in the Drepper's paper (see below).
// It atomically compares the value pointed by the address addr to the value expected
// and only if equal replaces *addr with desired. In either case it returns the value
// at *addr before the operation.
inline uint32_t cmpxchg(uint32_t *addr, uint32_t expected, uint32_t desired)
{
    uint32_t *expected_addr = &expected;
    __atomic_compare_exchange_n(addr, expected_addr, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return *expected_addr;
}

enum {
    UNLOCKED = 0,
    LOCKED_NO_WAITERS = 1,
    LOCKED_MAYBE_WAITERS = 2,
};

// This futex-based mutex implementation is based on the example "Mutex, Take 2"
// from the Ulrich Drepper's paper "Futexes Are Tricky" (https://dept-info.labri.fr/~denis/Enseignement/2008-IR/Articles/01-futex.pdf)
class fmutex {
public:
    fmutex() : _state(UNLOCKED) {}
    void lock()
    {
        uint32_t c;
        // If the state was UNLOCKED before cmpxchg, we do not have to do anything
        // just return after setting to LOCKED_NO_WAITERS
        if ((c = cmpxchg(&_state, UNLOCKED, LOCKED_NO_WAITERS)) != UNLOCKED) {
            do {
                // It was locked, so let us set the state to LOCKED_MAYBE_WAITERS.
                // It might be already in this state (1st part of if below) or
                // the state was LOCKED_NO_WAITERS so let us change it to LOCKED_MAYBE_WAITERS
                if (c == LOCKED_MAYBE_WAITERS ||
                    cmpxchg(&_state, LOCKED_NO_WAITERS, LOCKED_MAYBE_WAITERS) != UNLOCKED) {
                    // Wait until kernel tells us the state is different from LOCKED_MAYBE_WAITERS
                    syscall(SYS_futex, &_state, FUTEX_WAIT_PRIVATE, LOCKED_MAYBE_WAITERS, 0, 0, 0);
                }
                // At this point we are either because:
                // 1. The mutex was indeed UNLOCKED = the if condition above was false
                // 2. We were awoken when sleeping upon making the syscall FUTEX_WAIT_PRIVATE
                // So let us try to lock again. Because we do not know if there any waiters
                // we try to set to LOCKED_MAYBE_WAITERS and err on the safe side.
            } while ((c = cmpxchg(&_state, UNLOCKED, LOCKED_MAYBE_WAITERS)) != UNLOCKED);
        }
    }

    void unlock()
    {
        // Let us wake one waiter only if the state was LOCKED_MAYBE_WAITERS
        // Otherwise do nothing if uncontended
        if (__atomic_fetch_sub(&_state, 1, __ATOMIC_SEQ_CST) != LOCKED_NO_WAITERS) {
            _state = UNLOCKED;
            syscall(SYS_futex, &_state, FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
        }
    }
private:
    uint32_t _state;
};

void loop(int iterations)
{
    for (register int i=0; i<iterations; i++) {
        // To force gcc to not optimize this loop away
        asm volatile("" : : : "memory");
    }
}

int main(int argc, char** argv) {
    if (argc <= 2) {
        std::cerr << "Usage: " << argv[0] << " nthreads worklen <nmutexes>\n";
        return 1;
    }
    int nthreads = atoi(argv[1]);
    if (nthreads <= 0) {
        std::cerr << "Usage: " << argv[0] << " nthreads worklen <nmutexes>\n";
        return 2;
    }
    // "worklen" is the amount of work to do in each loop iteration, outside
    // the mutex. This reduces contention and makes the benchmark more
    // realistic and gives it the theoretic possibility of achieving better
    // benchmark numbers on multiple CPUs (because this "work" is done in
    // parallel.
    int worklen = atoi(argv[2]);
    if (worklen < 0) {
        std::cerr << "Usage: " << argv[0] << " nthreads worklen <nmutexes>\n";
        return 3;
    }

    // "nmutexes" is the number of mutexes the threads will be contending for
    // we will group threads by set of nthreads contending on individual mutex
    // to increase corresponding group counter
    int nmutexes = 1;
    if (argc >= 4) {
        nmutexes = atoi(argv[3]);
        if (nmutexes < 0)
            nmutexes = 1;
    }

    int concurrency = 0;
    cpu_set_t cs;
    sched_getaffinity(0, sizeof(cs), &cs);
    for (int i = 0; i < get_nprocs(); i++) {
        if (CPU_ISSET(i, &cs)) {
            concurrency++;
        }
    }
    std::cerr << "Running " << (nthreads * nmutexes) << " threads on " <<
            concurrency << " cores with " << nmutexes <<
            " mutexes. Worklen = " <<
            worklen << "\n";

    // Set secs to the desired number of seconds a measurement should
    // take. Note that the whole test will take several times longer than
    // secs, as we do several tests each lasting at least this long.
    double secs = 30.0;

    // Our mutex-protected operation will be a silly increment of a counter,
    // taking a tiny amount of time, but still can happen concurrently if
    // run very frequently from many cores in parallel.
    long counters[nmutexes] = {0};
    bool done = false;

    fmutex mut[nmutexes];
    std::vector<std::thread> threads;
    for (int m = 0; m < nmutexes; m++) {
        for (int i = 0; i < nthreads; i++) {
            threads.push_back(std::thread([&, m]() {
                while (!done) {
                    mut[m].lock();
                    counters[m]++;
                    mut[m].unlock();
                    loop(worklen);
                }
            }));
        }
    }
    threads.push_back(std::thread([&]() {
        std::this_thread::sleep_for(std::chrono::duration<double>(secs));
        done = true;
    }));
    for (auto &t : threads) {
        t.join();
    }
    long total = 0;
    for (int m = 0; m < nmutexes; m++) {
        total += counters[m];
    }
    std::cout << total << " counted in " << secs << " seconds (" << (total/secs) << " per sec)\n";

    return 0;
}
