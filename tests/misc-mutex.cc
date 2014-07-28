/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/sched.hh>
#include <osv/mutex.h>
#include <osv/spinlock.h>
#include <osv/clock.hh>

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
        //assert(m->getdepth()==1);
        //assert(m->owned());
        int val = *shared;
        *shared = val+1;
        m->unlock();
        if((i%100000)==0){
            std::cerr << char('A'+id);
        }
    }
}

template <typename T>
static void spinning_increment_thread(int id, T *m, long len, volatile long *shared)
{
    int i = 0;
    while(i<len){
        if (m->try_lock()) {
            assert(m->getdepth()==1);
            assert(m->owned());
            int val = *shared;
            *shared = val+1;
            m->unlock();
            if((i%100000)==0){
                std::cerr << char('A'+id);
            }
            i++;
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

// Test N concurrent threads using mutex, possibly each pinned to a different
// cpu (when pinned && N<=sched::cpus.size()).
template <typename T>
static void test(int N, long len, bool pinned, threadfunc<T> f)
{
    printf("Contended mutex test, %s, %d %spinned threads\n",typeinfo<T>::name(), N,
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
    printf("\n");
    printf("%d ns\n", (t2-t1)/len/N);
    if (f == &increment_thread<T>) {
        assert(shared==len*N);
    }
}

template <typename T>
static void measure_uncontended(long len)
{
    printf("Measuring uncontended %s lock/unlock: ", typeinfo<T>::name());
    T m;
    auto t1 = clock::get()->time();
    for (int i=0; i<len; i++) {
        m.lock();
        m.unlock();
    }
    auto t2 = clock::get()->time();
    printf("%d ns\n", (t2-t1)/len);
}

template <typename T>
static void show_size()
{
    printf("Size of %s: %d\n", typeinfo<T>::name(), sizeof(T));
}

// handoff_stressing_mutex is only for measure_uncontended<>. It stresses the
// "handoff" case of the lockfree mutex: before calling unlock() it increments
// count, then unlock() thinks it is racing with another lock and uses the
// handoff protocol, and after unlock() finishes, it decrements the count back.
class handoff_stressing_mutex : public mutex {
public:
    inline void unlock() {
        // Note: we set count to 2 instead of count++ because it's faster
        // and we want to measure the real unlock(), not to inflate the
        // measurement with these extra instructions.
        count.store(2, std::memory_order_relaxed);
        mutex::unlock();
        count.store(0, std::memory_order_relaxed);
    }
};

int main(int argc, char **argv)
{
    printf("Running mutex tests\n");

    printf("\nSizes of mutual exclusion primitives:\n");
    show_size<mutex>();
    show_size<spinlock>();

    printf("\n==== BENCHMARK 1 ====\nUncontended single-thread lock/unlock cycle:\n");
    measure_uncontended<mutex>(50000000);
    measure_uncontended<handoff_stressing_mutex>(50000000);
    measure_uncontended<spinlock>(50000000);


    // The lock-free mutex's biggest challenge is what to do in unlock() when
    // we want to wake a concurrent lock() but the wait queue is still empty
    // (because the concurrent lock() didn't yet put itself there). The more
    // concurrent locking threads we have, the less chance we have to find an
    // empty queue, so somewhat counter-intuitively, the test with 2 threads
    // stresses the "handoff" feature more than tests with more threads.

    printf("\n==== BENCHMARK 2 ====\nContended tests using increment_thread:\n");
    auto lff = increment_thread<mutex>;
    int n = 1000000;
    test<mutex>(2, n, true, lff);
    test<mutex>((int)sched::cpus.size(), n, true, lff);
    test<mutex>(20, n, false, lff);

    auto spf = increment_thread<spinlock>;
    test<spinlock>(2, n, true, spf);
    test<spinlock>((int)sched::cpus.size(), n, true, spf);
    test<spinlock>(20, n, false, spf);

    printf("\n==== MISC TESTS ====\n");
    printf("\n\nTrylock tests using spinning_increment_thread:\n");
    lff = spinning_increment_thread<mutex>;
    test<mutex>(2, n, true, lff);

    printf("\n\nMutual exclusion test using checker_thread:\n");
    lff = checker_thread<mutex>;
    n = 100000;
    test<mutex>(2, n, true, lff);
    test<mutex>((int)sched::cpus.size(), n, true, lff);
    test<mutex>(20, n, false, lff);

    printf("mutex tests succeeded\n");
}
