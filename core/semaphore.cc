/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/semaphore.hh>

semaphore::semaphore(unsigned val)
    : _val(val)
{
}

void semaphore::post_unlocked(unsigned units)
{
    _val += units;
    auto i = _waiters.begin();
    while (_val > 0 && i != _waiters.end()) {
        auto wr = i++;
        if (wr->units <= _val) {
            _val -= wr->units;
            wr->owner->wake();
            wr->owner = nullptr;
            _waiters.erase(wr);
        }
    }
}

bool semaphore::wait(unsigned units, sched::timer* tmr)
{
    wait_record wr;
    wr.owner = nullptr;

    std::lock_guard<mutex> guard(_mtx);

    if (_val >= units) {
        _val -= units;
        return true;
    } else {
        wr.owner = sched::thread::current();
        wr.units = units;
        _waiters.push_back(wr);
    }

    sched::thread::wait_until(_mtx,
            [&] { return (tmr && tmr->expired()) || !wr.owner; });

    // if wr.owner, it's a timeout - post() didn't wake us and didn't decrease
    // the semaphore's value for us. Note we are holding the mutex, so there
    // can be no race with post().
    return !wr.owner;
 }

bool semaphore::trywait(unsigned units)
{
    bool ok = false;
    WITH_LOCK(_mtx) {
        if (_val > units) {
            _val -= units;
            ok = true;
        }
    }

    return ok;
}




