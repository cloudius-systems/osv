#ifndef MUTEX_HH
#define MUTEX_HH

#include <mutex>
#include <list>

namespace sched {
class thread;
}

class mutex {
public:
    void lock();
    bool try_lock();
    void unlock();
private:
    bool _locked;
    std::list<sched::thread*> _waiters;
};

template <class Lock, class Func>
auto with_lock(Lock& lock, Func func) -> decltype(func())
{
    std::lock_guard<Lock> guard(lock);
    return func();
}

#endif
