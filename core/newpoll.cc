#include <osv/sched.hh>
#include <osv/newpoll.hh>

namespace osv {
namespace newpoll {

poller::poller()
    : _timer(new sched::timer(*sched::thread::current()))
{
}

poller::~poller() {
}

void poller::add(pollable *item) {
    // TODO: check if item isn't already in the list...
    item->_owner = sched::thread::current();
    _items.push_front(item);
}

void poller::del(pollable *item) {
    auto prev = _items.before_begin();
    for (auto it = _items.begin(); it != _items.end(); ++it) {
        if ((*it) == item) {
            _items.erase_after(prev);
            break;
        }
        prev = it;
    }
    // TODO: complain if item wasn't in the list...
}

void pollable::wake() {
    // could use wake_with() but we're not afraid a thread will go away.
    _woken.store(true, std::memory_order_relaxed);
    if (_owner)
        _owner->wake();
}

void poller::set_timer(std::chrono::high_resolution_clock::time_point when)
{
    // In OSv, high_resolution_clock has the same epoch as wall_clock -
    // the Unix epoch. TODO: Add a central function to do this conversion.
    osv::clock::wall::time_point tp(when.time_since_epoch());
    osv::clock::uptime::time_point ut(tp - osv::clock::wall::boot_time());
    _timer->reset(ut);
}

bool poller::expired()
{
    if (_timer->expired()) {
        // make sure that expired() only returns true once because of one
        // expiration (same as timerfd we're trying to mimic).
        _timer->cancel();
        return true;
    } else {
        return false;
    }
}

// TODO: This is O(N) poll()-like code. We better use epoll-like
// implementation, where at wake() time we put the pollable on a
// woken list. But for small number of items, it's fine.

void poller::wait()
{
    sched::thread::wait_until([&] {
        for (auto item : _items) {
            if (item->poll())
                return true;
        }
        return _timer->expired();
    });
}

void poller::process()
{
    // Iterate over the items using a technique that is ugly, but allows the
    // on_wake() function to use poller::del() to remove the item for which
    // the callback is being run.
    for (auto it = _items.begin(); it != _items.end(); ) {
        auto item = *it;
        // move the iterator before running the callback which might
        // invalidate it.
        ++it;
        if (item->read()) {
            item->on_wake();
        }
    }
}


}
}
