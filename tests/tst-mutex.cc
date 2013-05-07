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
    //debug("Starting thread %d(%d)\n", id, sched::thread::current()->id());
    for(int i=0; i<len; i++){
        m->lock();
        int val = *shared;
        *shared = val+1;
        m->unlock();
        if((i%100000)==0){
            debug("%d ",id);
        }
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
static void test(int N, long len, bool pinned)
{
    debug("Contended mutex test, %s, %d %spinned threads\n",typeinfo<T>::name(), N,
            pinned ? "" : "non-");
    //free(name);
    assert (!pinned || (unsigned int)N <= sched::cpus.size());
    long shared=0;
    T m;
    sched::thread *threads[N];
    for(int i = 0; i < N; i++) {
        threads[i]= new sched::thread([i, len, &m, &shared] {
            increment_thread(i, &m, len, &shared);
        }, pinned ? sched::thread::attr(sched::cpus[i]) : sched::thread::attr());
    }
    for(int i = 0; i < N; i++) {
        threads[i]->start();
    }
    for(int i = 0; i < N; i++){
        threads[i]->join();
        delete threads[i];
    }
    debug("\n");
    assert(shared==len*N);
}

// Test N concurrent threads using mutex, each pinned to a different cpu (N<=sched::cpus.size()).
template <typename T>
static void measure_uncontended(long len)
{
    debug("Measuring uncontended %s lock/unlock: ",typeinfo<T>::name());
    T m;
    auto t1 = clock::get()->time();
    for (int i=0; i<len; i++) {
        m.lock();
        m.unlock();
    }
    auto t2 = clock::get()->time();
    debug ("%d ns\n", (t2-t1)/len);
}

int main(int argc, char **argv)
{
    debug("Running mutex tests\n");

    measure_uncontended<lockfree::mutex>(10000000);
    measure_uncontended<mutex>(10000000);
    measure_uncontended<spinlock>(10000000);

    test<lockfree::mutex>((int)sched::cpus.size(), 1000000, true);
    test<lockfree::mutex>(2, 1000000, true);
    test<lockfree::mutex>(20, 1000000, false);

    test<mutex>((int)sched::cpus.size(), 1000000, true);
    test<mutex>(2, 1000000, true);
    test<mutex>(20, 1000000, false);

    test<spinlock>((int)sched::cpus.size(), 1000000, true);
    test<spinlock>(2, 1000000, true);
    test<spinlock>(20, 1000000, false);


    debug("mutex tests succeeded\n");
}
