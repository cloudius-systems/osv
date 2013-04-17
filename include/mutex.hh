#ifndef MUTEX_HH
#define MUTEX_HH

#include <mutex>
#include <list>
#include <osv/mutex.h>

class mutex {
public:
    mutex() { mutex_init(&_mutex); }
    ~mutex() { mutex_destroy(&_mutex); }
    void lock();
    bool try_lock();
    void unlock();
    // getdepth() should only be used by the thread holding the lock
    inline unsigned int getdepth() const { return _mutex._depth; }
private:
    mutex_t _mutex;
};

// Use mutex instead, except where impossible
class spinlock {
public:
    void lock();
    void unlock();
private:
    spinlock_t _lock;
};

template <class Lock, class Func>
auto with_lock(Lock& lock, Func func) -> decltype(func())
{
    std::lock_guard<Lock> guard(lock);
    return func();
}

#endif
