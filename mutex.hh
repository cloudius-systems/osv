#ifndef MUTEX_HH
#define MUTEX_HH

#include <mutex>

class mutex {
public:
    void lock();
    bool try_lock();
    void unlock();
};

template <class Lock, class Func>
auto with_lock(Lock& lock, Func func) -> decltype(func())
{
    std::lock_guard<Lock> guard(lock);
    return func();
}

#endif
