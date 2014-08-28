/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Implement the Linux epoll(7) functions in OSV

// NOTE: This is an inefficient implementation, in which epoll_wait() calls
// poll(), thereby negating all the performance benefits of using epoll and
// not poll. This is only a temporary implementation, for getting the
// functionality of epoll which Java needs - but not its performance.

#include <sys/epoll.h>
#include <sys/poll.h>
#include <memory>
#include <stdio.h>
#include <errno.h>


#include <osv/file.h>
#include <osv/poll.h>
#include <fs/fs.hh>
#include <lockfree/ring.hh>

#include <osv/debug.hh>
#include <unordered_map>
#include <boost/range/algorithm/find.hpp>
#include <algorithm>

#include <osv/trace.hh>
TRACEPOINT(trace_epoll_create, "returned fd=%d", int);
TRACEPOINT(trace_epoll_ctl, "epfd=%d, fd=%d, op=%s event=0x%x", int, int, const char*, int);
TRACEPOINT(trace_epoll_wait, "epfd=%d, maxevents=%d, timeout=%d", int, int, int);
TRACEPOINT(trace_epoll_ready, "fd=%d file=%p, event=0x%x", int, file*, int);

// We implement epoll using poll(), and therefore need to convert epoll's
// event bits to and poll(). These are mostly the same, so the conversion
// is trivial, but we verify this here with static_asserts. We additionally
// support the epoll-only EPOLLET and EPOLLONESHOT.
static_assert(POLLIN == EPOLLIN, "POLLIN!=EPOLLIN");
static_assert(POLLOUT == EPOLLOUT, "POLLOUT!=EPOLLOUT");
static_assert(POLLRDHUP == EPOLLRDHUP, "POLLRDHUP!=EPOLLRDHUP");
static_assert(POLLPRI == EPOLLPRI, "POLLPRI!=EPOLLPRI");
static_assert(POLLERR == EPOLLERR, "POLLERR!=EPOLLERR");
static_assert(POLLHUP == EPOLLHUP, "POLLHUP!=EPOLLHUP");
constexpr int SUPPORTED_EVENTS =
        EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLPRI | EPOLLERR | EPOLLHUP |
        EPOLLET | EPOLLONESHOT;
inline uint32_t events_epoll_to_poll(uint32_t e)
{
    assert (!(e & ~SUPPORTED_EVENTS));
    return e;
}
inline uint32_t events_poll_to_epoll(uint32_t e)
{
    assert (!(e & ~SUPPORTED_EVENTS));
    return e;
}

struct registered_epoll : epoll_event {
    int last_poll_wake_count; // For implementing EPOLLET
    registered_epoll(epoll_event e, int c) :
        epoll_event(e), last_poll_wake_count(c) {}
};

class epoll_file final : public special_file {

    // lock ordering (fp == some file being polled):
    //    f_lock > fp->f_lock
    //    fp->f_lock > _activity_lock
    // we never hold both f_lock and activity_lock.

    // protected by f_lock:
    std::unordered_map<epoll_key, registered_epoll> map;
    mutex _activity_lock;
    // below, all protected by _activity_lock:
    std::unordered_set<epoll_key> _activity;
    waitqueue _waiters;
    ring_spsc<epoll_key, 256> _activity_ring;
    std::atomic<bool> _activity_ring_overflow = { false };
    sched::thread_handle _activity_ring_owner;
public:
    epoll_file()
        : special_file(0, DTYPE_UNSPEC)
    {
    }
    virtual int close() override {
        WITH_LOCK(f_lock) {
            for (auto& e : map) {
                e.first._file->epoll_del({ this, e.first });
            }
        }
        return 0;
    }
    int add(epoll_key key, struct epoll_event *event)
    {
        auto fp = key._file;
        WITH_LOCK(f_lock) {
            if (map.count(key)) {
                return EEXIST;
            }
            // I used poll_wake_count-1, to ensure EPOLLET returns once when
            // registering an epoll after data is already available.
            map.emplace(key,
                    registered_epoll(*event, fp->poll_wake_count - 1));
            fp->epoll_add({ this, key});
        }
        if (fp->poll(events_epoll_to_poll(event->events))) {
            wake(key);
        }
        return 0;
    }
    int mod(epoll_key key, struct epoll_event *event)
    {
        auto fp = key._file;
        WITH_LOCK(f_lock) {
            try {
                map.at(key) = registered_epoll(*event, fp->poll_wake_count - 1);
            } catch (std::out_of_range &e) {
                return ENOENT;
            }
            fp->epoll_add({ this, key });
        }
        if (fp->poll(events_epoll_to_poll(event->events))) {
            wake(key);
        }
        return 0;
    }
    int del(epoll_key key)
    {
        WITH_LOCK(f_lock) {
            if (map.erase(key)) {
                key._file->epoll_del({ this, key });
                return 0;
            } else {
                return ENOENT;
            }
        }
    }
    int wait(struct epoll_event *events, int maxevents, int timeout_ms)
    {
        auto tmo = parse_poll_timeout(timeout_ms);
        sched::timer tmr(*sched::thread::current());
        if (tmo) {
            tmr.set(*tmo);
        }
        int nr = 0;
        WITH_LOCK(_activity_lock) {
            while (!tmr.expired() && nr == 0) {
                if (tmo) {
                    _activity_ring_owner.reset(*sched::thread::current());
                    sched::thread::wait_for(_activity_lock,
                            _waiters,
                            tmr,
                            [&] { return !_activity.empty(); },
                            [&] { return !_activity_ring.empty(); },
                            [&] { return _activity_ring_overflow.load(std::memory_order_relaxed); }
                    );
                    _activity_ring_owner.clear();
                }

                flush_activity_ring();
                // We need to drop f_lock file calling file::poll(), so move _activity to
                // local storage for processing.  Since _activity_mutex is internal to
                // f_lock, we need to drop it as well.
                auto activity = std::move(_activity);
                assert(_activity.empty());
                DROP_LOCK(_activity_lock) {
                    nr = process_activity(activity, events, maxevents);
                }
                // move back !EPOLLET back to main storage
                if (_activity.empty()) {
                    // nothing happened, move entire set back in
                    _activity = std::move(activity);
                } else {
                    // move item by item (realistically a copy)
                    std::move(activity.begin(), activity.end(),
                            std::inserter(_activity, _activity.begin()));
                }
                if (!tmo) {
                    break;
                }
            }
        }
        return nr;
    }
    int process_activity(std::unordered_set<epoll_key>& activity,
                         epoll_event* events, int maxevents) {
        int nr = 0;
        WITH_LOCK(f_lock) {
            auto i = activity.begin();
            while (i != activity.end() && nr < maxevents) {
                epoll_key key = *i;
                auto found = map.find(key);
                auto cur = i++;
                if (found == map.end()) {
                    activity.erase(cur);
                    continue; // raced
                }
                registered_epoll r_e = found->second;
                int active = 0;
                if (r_e.events) {
                    active = key._file->poll(events_epoll_to_poll(r_e.events));
                }
                active = events_poll_to_epoll(active);
                if (!active || (r_e.events & EPOLLET)) {
                    activity.erase(cur);
                } else {
                    key._file->epoll_add({ this, key });
                }
                if (!active) {
                    continue;
                }
                if (r_e.events & EPOLLONESHOT) {
                    // since we dropped the lock, the key may not be there anymore
                    auto i = map.find(key);
                    if (i != map.end()) {
                        i->second.events = 0;
                        key._file->epoll_del({ this, key });
                    }
                }
                trace_epoll_ready(key._fd, key._file, active);
                events[nr].data = r_e.data;
                events[nr].events = active;
                ++nr;
            }
        }
        return nr;
    }
    void flush_activity_ring() {
        epoll_key ep;
        while (_activity_ring.pop(ep)) {
            _activity.insert(ep);
        }
        if (_activity_ring_overflow.load(std::memory_order_relaxed)) {
            _activity_ring_overflow.store(false, std::memory_order_relaxed);
            for (auto&& x : map) {
                _activity.insert(x.first);
            }
        }
        // events on _activity_ring only wake up one waiter, so wake up all the rest  now.
        // we need to do this even if no events were received, since if we exit, then
        // _activity_ring_owner will remain unset.
        _waiters.wake_all(_activity_lock);
    }
    void wake(epoll_key key) {
        WITH_LOCK(_activity_lock) {
            auto ins = _activity.insert(key);
            if (ins.second) {
                _waiters.wake_all(_activity_lock);
            }
        }
    }
    void wake_in_rcu(epoll_key key) {
        if (!_activity_ring.push(key)) {
            _activity_ring_overflow.store(true, std::memory_order_relaxed);
        }
        _activity_ring_owner.wake();
    }
};

int epoll_create(int size)
{
    // Note we ignore the size parameter. There's no point in checking it's
    // positive, and on the other hand we can't trust it being meaningful
    // because Linux ignores it too.
    return epoll_create1(0);
}

int epoll_create1(int flags)
{
    flags &= ~EPOLL_CLOEXEC;
    assert(!flags);
    try {
        fileref f = make_file<epoll_file>();
        fdesc fd(f);
        trace_epoll_create(fd.get());
        return fd.release();
    } catch (int error) {
        errno = error;
        trace_epoll_create(-1);
        return -1;
    }
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    trace_epoll_ctl(epfd, fd,
            op==EPOLL_CTL_ADD ? "EPOLL_CTL_ADD" :
            op==EPOLL_CTL_MOD ? "EPOLL_CTL_MOD" :
            op==EPOLL_CTL_DEL ? "EPOLL_CTL_DEL" : "?",
                    event->events);
    fileref epfr(fileref_from_fd(epfd));
    if (!epfr) {
        errno = EBADF;
        return -1;
    }

    auto epo = dynamic_cast<epoll_file*>(epfr.get());
    if (!epo) {
        errno = EINVAL;
        return -1;
    }

    int error = 0;
    fileref fp = fileref_from_fd(fd);
    if (!fp) {
        errno = EBADF;
        return -1;
    }
    epoll_key key{fd, fp.get()};

    switch (op) {
    case EPOLL_CTL_ADD:
        error = epo->add(key, event);
        break;
    case EPOLL_CTL_MOD:
        error = epo->mod(key, event);
        break;
    case EPOLL_CTL_DEL:
        error = epo->del(key);
        break;
    default:
        error = EINVAL;
    }

    if (error) {
        errno = error;
        return -1;
    } else {
        return 0;
    }
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout_ms)
{
    trace_epoll_wait(epfd, maxevents, timeout_ms);
    fileref epfr(fileref_from_fd(epfd));
    if (!epfr) {
        errno = EBADF;
        return -1;
    }

    auto epo = dynamic_cast<epoll_file*>(epfr.get());
    if (!epo || maxevents <= 0) {
        errno = EINVAL;
        return -1;
    }

    return epo->wait(events, maxevents, timeout_ms);
}

void epoll_file_closed(epoll_ptr ptr)
{
    ptr.epoll->del(ptr.key);
}

void epoll_wake(const epoll_ptr& ep)
{
    ep.epoll->wake(ep.key);
}

void epoll_wake_in_rcu(const epoll_ptr& ep)
{
    ep.epoll->wake_in_rcu(ep.key);
}
