#include "mutex.hh"
#include "sched.hh"

void mutex::lock()
{
    // FIXME: use atomics
    if (!_locked) {
        _locked = true;
        return;
    } else {
        auto me = sched::thread::current();
        _waiters.push_back(me);
        sched::thread::wait_until([=] {
            return !_locked && _waiters.front() == me;
        });
        _waiters.pop_front();
    }
}

bool mutex::try_lock()
{
    if (_locked) {
        return false;
    } else {
        _locked = true;
        return true;
    }
}

void mutex::unlock()
{
    _locked = false;
    if (!_waiters.empty()) {
        _waiters.front()->wake();
    }
}

void spinlock::lock()
{
    while (__sync_lock_test_and_set(&_locked, 1))
        ;
}

void spinlock::unlock()
{
    __sync_lock_release(&_locked, 0);
}
