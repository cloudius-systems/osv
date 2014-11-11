/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef NEWPOLL_API_H
#define NEWPOLL_API_H

// An osv::newpoll::poller allows waiting on a variable number of
// osv::newpoll:pollable objects (plus a timer), similar in spirit
// to epoll() and file descriptors respectively, but the pollable
// objects are not file descriptors. Another difference is that one
// pollable can only be waited on by a single thread at a time, so
// it requires less locking.

#include <functional>
#include <forward_list>
#include <memory>
#include <chrono>

namespace sched {
class thread;
class timer;
}

namespace osv {
namespace newpoll {

class poller;

class pollable {
public:
    // The on_wake() callback is called by poller:process() when it notices
    // an event becomes ready. Note that this callback is called in the
    // context of the process(), *not* in the context of wake().
    virtual void on_wake() = 0;
private:
    sched::thread *_owner = nullptr;
    std::atomic<bool> _woken {false};
public:
    void wake();
    bool poll() {
        return _woken.load(std::memory_order_relaxed);
    }
    bool read() {
        // TODO: if we're sure the wake() can only happen from an interrupt
        // handler and not run directly on another CPU, cli/sti might be
        // enough instead of atomic exchange. The problem is what if wake()
        // is called in the middle of read(), we can lose an event.
        return _woken.exchange(false, std::memory_order_relaxed);
    }
    // A waiter object cannot be copied or moved, as wake() on the copy will
    // change the boolean on the copy of the content - not the original
    // content on which wait() is waiting on. This is why normally we operate
    // on pointers to waiters.
    pollable(const pollable &) = delete;
    pollable &operator=(const pollable &) = delete;
    pollable(pollable &&) = delete;
    pollable &operator=(pollable &&) = delete;
    pollable() = default;

    friend class poller;
};

class poller {
    std::forward_list<pollable *> _items;
    // TODO: make timer just a normal pollable instead of a separate feature
    std::unique_ptr<sched::timer> _timer;
public:
    poller();
    // Because sched::timer is an incomplete type, _timer's destructor cannot
    // be determined here. So we need to only declare ~poller here, but define
    // it (as empty!) in the .cc file. Yet another ugly part of C++ :-(
    ~poller();

    void add(pollable *item);
    void del(pollable *item);

    // wait() waits for one more previously-add()ed events to be woken
    void wait();

    // process() runs the callbacks of events that are woken (and marks
    // each such processed event as non-woken again). Unless called after
    // wait(), this can be zero events.
    void process();

    // Currently, the timer is not a normal pollable and doesn't have a
    // callback. Rather, after calling wait(), do something if expired().
    void set_timer(std::chrono::high_resolution_clock::time_point when);
    bool expired();
};

}
}

#endif
