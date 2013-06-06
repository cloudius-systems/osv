#ifndef INCLUDED_OSV_INDIRECT_HH
#define INCLUDED_OSV_INDIRECT_HH

#include <atomic>

// A lazy_indirect<T> is a small object (8 bytes) which pretends to contain
// an object of arbitrarily-sized type T, while actually allocating one
// dynamically only on first use. This allocation is done in a thread-safe
// manner, even if several threads race to use the object first.
// T should have a default (zero-argument) constructor.
//
// lazy_indirect<T>'s constructor merely zeros the object's 8 bytes, so
// it is ok to cast zeroed memory to lazy_indirect<T> - see an example
// of this use in pthread.cc. However, remember in this case to call
// the lazy_indirect<T>::~lazy_indirect eventually, otherwise the memory used
// to allocate T will leak.

template <typename T>
struct lazy_indirect {
private:
    std::atomic<T*> real;
public:
    lazy_indirect() : real(0) { }
    ~lazy_indirect() { delete real; }
    T *get() {
        T *ret = real.load(std::memory_order_consume);
        if (ret) {
            return ret;
        }
        // Otherwise, we need to allocate the real mutex. Take care that
        // several threads don't allocate the same mutex the same time. We use
        // optimistic allocation here, assuming the object is cheap to
        // allocate; Alternatively we could could have also used a mutex.
        ret = new T;
        T *val = 0;
        if (real.compare_exchange_strong(val, ret,
                std::memory_order_release, std::memory_order_consume)) {
            return ret;
        } else {
            // We lost the race - free the object we allocated.
            delete ret;
            return val; // val == real.load(std::memory_order_consume)
        }
    }
};

#endif /* INCLUDED_OSV_INDIRECT_HH */
