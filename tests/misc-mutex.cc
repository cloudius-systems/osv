/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "sched.hh"
#include "debug.hh"
#include "lockfree/mutex.hh"
#include <osv/mutex.h>
#include "drivers/clock.hh"

#include <string.h>

// increment_thread loops() non-atomically incrementing a shared value.
// If N threads like this run concurrently, at the end the sum will
// not end up N*len.
// NOTE: If the threads run separately, they will not interfere with
// each other, so make sure the test is long enough that the first
// thread doesn't finish before the second starts!
template <typename T>
static void increment_thread(int id, T *m, long len, volatile long *shared)
{
    for(int i=0; i<len; i++){
        m->lock();
        assert(m->getdepth()==1);
        assert(m->owned());
        int val = *shared;
        *shared = val+1;
        m->unlock();
        if((i%100000)==0){
            debug("%d ",id);
        }
    }
}

template <typename T>
using threadfunc =  decltype(increment_thread<T>);

// checker_thread loops() sets a shared value to a known number, and
// verifies that no other thread changes it. It checks the mutual-
// exclusion capabilities of the mutex better than increment_thread.
template <typename T>
static void checker_thread(int id, T *m, long len, volatile long *shared)
{
    for (int i = 0; i < len; i++) {
        m->lock();
        assert(m->getdepth()==1);
        assert(m->owned());
        *shared = id;
        for (int j = 0; j < 1000; j++)
            assert(*shared == id);
        sched::thread::yield();
        for (int j = 0; j < 1000; j++)
            assert(*shared == id);
        m->unlock();
    }
}



// Unfortunately, C++ lacks the trivial feature of converting a type's name,
// in compile time, to a string (akin to the C preprocessor's "#" feature).
// Here is a neat trick to replace it - use typeinfo<T>::name() to get a
// constant string name of the type.
#include <cxxabi.h>
template <typename T>
class typeinfo {
private:
    static const char *_name;
public:
    static const char *name() {
        int status;
        if (!_name)
            _name = abi::__cxa_demangle(typeid(T).name(), 0, 0, &status);
        return _name;
    }
};
template<typename T> const char *typeinfo<T>::_name = nullptr;

// Test N concurrent threads using mutex, each pinned to a different cpu (N<=sched::cpus.size()).
template <typename T>
static void test(int N, long len, bool pinned, threadfunc<T> f)
{
    debug("Contended mutex test, %s, %d %spinned threads\n",typeinfo<T>::name(), N,
            pinned ? "" : "non-");
    assert (!pinned || (unsigned int)N <= sched::cpus.size());
    long shared=0;
    T m;
    sched::thread *threads[N];
    for(int i = 0; i < N; i++) {
        threads[i]= new sched::thread([i, len, &m, &shared, f] {
            f(i, &m, len, &shared);
        }, pinned ? sched::thread::attr().pin(sched::cpus[i]) : sched::thread::attr());
    }
    auto t1 = clock::get()->time();
    for(int i = 0; i < N; i++) {
        threads[i]->start();
    }
    for(int i = 0; i < N; i++){
        threads[i]->join();
        delete threads[i];
    }
    auto t2 = clock::get()->time();
    debug("\n");
    debug ("%d ns\n", (t2-t1)/len);
    if (f == &increment_thread<T>) {
        assert(shared==len*N);
    }
}

// Test N concurrent threads using mutex, each pinned to a different cpu (N<=sched::cpus.size()).
template <typename T>
static void measure_uncontended(long len)
{
    debug("Measuring uncontended %s lock/unlock: ", typeinfo<T>::name());
    T m;
    auto t1 = clock::get()->time();
    for (int i=0; i<len; i++) {
        m.lock();
        m.unlock();
    }
    auto t2 = clock::get()->time();
    debug ("%d ns\n", (t2-t1)/len);
}

template <typename T>
static void show_size()
{
    debug("Size of %s: %d\n", typeinfo<T>::name(), sizeof(T));
}

// handoff_stressing_mutex is only for measure_uncontended<>. It stresses the
// "handoff" case of the lockfree mutex - before calling unlock() it
// always increments count (actually, sets it to 2, because we assume
// it's 1), then calls unlock() which thinks it is racing with another
// lock and uses the handoff protocol, and after unlock() finishes, it
// decrements the count back.
class handoff_stressing_mutex : public lockfree::mutex {
public:
    inline void unlock() {
        // Note: we set count to 2 instead of count++ because it's faster
        // and we want to measure the real unlock(), not to inflate the
        // measurement with these extra instructions.
#if CONFIG_UP
        count = 2;
#else
        count.store(2, std::memory_order_relaxed);
#endif
        lockfree::mutex::unlock();
#if CONFIG_UP
        count = 0;
#else
        count.store(0, std::memory_order_relaxed);
#endif
    }
};

int main(int argc, char **argv)
{
    debug("Running mutex tests\n");
    show_size<lockfree::mutex>();
#ifndef LOCKFREE_MUTEX
    show_size<mutex>();
#endif
    show_size<spinlock>();

    measure_uncontended<lockfree::mutex>(10000000);
    measure_uncontended<handoff_stressing_mutex>(10000000);
#ifndef LOCKFREE_MUTEX
    measure_uncontended<mutex>(10000000);
#endif
    measure_uncontended<spinlock>(10000000);


    // The lockfree mutex's biggest challenge is what to do in unlock() when
    // we want to wake a concurrent lock() but the wait queue is still empty
    // (because the concurrent lock() didn't yet put itself there). The more
    // concurrent locking threads we have, the less chance we have to find an
    // empty queue, so somewhat counter-intuitively, the test with 2 threads
    // stresses the "handoff" feature more than tests with more threads.


    auto lff = increment_thread<lockfree::mutex>;
    int n = 10000;
    test<lockfree::mutex>(2, n, true, lff);
    test<lockfree::mutex>((int)sched::cpus.size(), n, true, lff);
    test<lockfree::mutex>(20, n, false, lff);

    n = 1000000;
    test<lockfree::mutex>(2, n, true, lff);
    test<lockfree::mutex>((int)sched::cpus.size(), n, true, lff);
    test<lockfree::mutex>(20, n, false, lff);

    lff = checker_thread<lockfree::mutex>;
    n = 100000;
    test<lockfree::mutex>(2, n, true, lff);
    test<lockfree::mutex>((int)sched::cpus.size(), n, true, lff);
    test<lockfree::mutex>(20, n, false, lff);

#ifndef LOCKFREE_MUTEX
    auto f = increment_thread<mutex>;
    test<mutex>((int)sched::cpus.size(), 1000000, true, f);
    test<mutex>(2, 1000000, true, f);
    test<mutex>(20, 1000000, false, f);
#endif

//    test<spinlock>((int)sched::cpus.size(), 1000000, true);
//    test<spinlock>(2, 1000000, true);
//    test<spinlock>(20, 1000000, false);


    debug("mutex tests succeeded\n");
}
