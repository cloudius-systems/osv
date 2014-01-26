/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <map>
#include <list>
#include <errno.h>
#include <osv/sched.hh>
#include "osv/trace.hh"

#include <bsd/porting/netport.h>
#include <bsd/porting/synch.h>
#include <bsd/porting/sync_stub.h>
#include <bsd/sys/sys/param.h>

TRACEPOINT(trace_synch_msleep, "chan=%p mtx=%p timo_hz=%d", void *, void *, int);
TRACEPOINT(trace_synch_msleep_wait, "chan=%p timo_hz=%d", void *, int);
TRACEPOINT(trace_synch_msleep_expired, "chan=%p", void *);
TRACEPOINT(trace_synch_wakeup, "chan=%p", void *);
TRACEPOINT(trace_synch_wakeup_waking, "chan=%p thread=%p", void *, void *);
TRACEPOINT(trace_synch_wakeup_one, "chan=%p", void *);
TRACEPOINT(trace_synch_wakeup_one_waking, "chan=%p thread=%p", void *, void *);

struct synch_thread {
    sched::thread* _thread;
    bool _awake;
};

class synch_port {
public:
    synch_port() { mutex_init(&_lock); }
    virtual ~synch_port() { mutex_destroy(&_lock); }

    static synch_port* instance() {
        if (_instance == nullptr) {
            _instance = new synch_port();
        }
        return (_instance);
    }

    int _msleep(void *chan, struct mtx *mtx, int priority, const char *wmesg,
         int timo);

    void wakeup(void* chan);

    void wakeup_one(void* chan);

private:
    static synch_port* _instance;

    mutex_t _lock;

    /* Make sure multimap is ordered */
    std::multimap<void *, synch_thread *> _evlist;
};

synch_port* synch_port::_instance = nullptr;

int synch_port::_msleep(void *chan, struct mtx *mtx,
    int priority, const char *wmesg, int timo_hz)
{
    trace_synch_msleep(chan, mtx, timo_hz);

    mutex_t *wait_lock = nullptr;

    // Init the wait
    synch_thread wait;
    wait._thread = sched::thread::current();
    wait._awake = false;

    if (mtx) {
        wait_lock = &mtx->_mutex;
    }

    if (chan) {
        mutex_lock(&_lock);
        _evlist.insert(std::make_pair(chan, &wait));
        mutex_unlock(&_lock);
    }

    sched::timer t(*sched::thread::current());

    if (timo_hz) {
        u64 nanoseconds = ticks2ns(timo_hz);
        t.set(std::chrono::nanoseconds(nanoseconds));
    }

    trace_synch_msleep_wait(chan, timo_hz);

    if (wait_lock) {
        mutex_unlock(wait_lock);
    }

    bool interrupted = false;

    try
    {
        sched::thread::wait_until_interruptible([&] {
            return ( (timo_hz && t.expired()) ||
                     (wait._awake) );
        });
    }
    catch (int e)
    {
        assert(e == EINTR);
        interrupted = true;
    }

    if (!(priority & PDROP) && wait_lock) {
        mutex_lock(wait_lock);
    }
    // msleep timeout
    if (!wait._awake) {
        trace_synch_msleep_expired(chan);
        if (chan) {
            // A pointer to the local "wait" may still be on the list -
            // need to remove it before we can return:
            // A pointer to the local "wait" may still be on the list -
            // need to remove it before we can return:
            mutex_lock(&_lock);
            auto ppp = _evlist.equal_range(chan);
            for (auto it=ppp.first; it!=ppp.second; ++it) {
                if ((*it).second == &wait) {
                    _evlist.erase(it);
                    break;
                }
            }
            mutex_unlock(&_lock);
        }
        return interrupted ? (EINTR) : (EWOULDBLOCK);
    }

    return (0);
}

void synch_port::wakeup(void* chan)
{
    trace_synch_wakeup(chan);

    mutex_lock(&_lock);
    auto ppp = _evlist.equal_range(chan);

    for (auto it=ppp.first; it!=ppp.second; ++it) {
        synch_thread* wait = (*it).second;
        trace_synch_wakeup_waking(chan, wait->_thread);
        wait->_thread->wake_with([&] { wait->_awake = true; });
    }
    _evlist.erase(ppp.first, ppp.second);
    mutex_unlock(&_lock);
}

void synch_port::wakeup_one(void* chan)
{
    trace_synch_wakeup_one(chan);

    mutex_lock(&_lock);
    auto ppp = _evlist.equal_range(chan);
    auto it = ppp.first;
    if (it != _evlist.end()) {
        synch_thread* wait = (*it).second;
        _evlist.erase(it);
        trace_synch_wakeup_one_waking(chan, wait->_thread);
        wait->_thread->wake_with([&] { wait->_awake = true; });
    }
    mutex_unlock(&_lock);
}

extern "C" int _msleep(void *chan, struct mtx *mtx, int priority, const char *wmesg,
     int timo)
{
    return (synch_port::instance()->_msleep(chan, mtx, priority, wmesg, timo));
}

extern "C" int tsleep(void *chan, int priority, const char *wmesg, int timo)
{
    return (_msleep(chan, 0, priority, wmesg, timo));
}

extern "C" void bsd_pause(const char *wmesg, int timo)
{
    _msleep(0, 0, 0, wmesg, timo);
}

extern "C" void wakeup(void* chan)
{
    synch_port::instance()->wakeup(chan);
}

extern "C" void wakeup_one(void* chan)
{
    synch_port::instance()->wakeup_one(chan);
}
