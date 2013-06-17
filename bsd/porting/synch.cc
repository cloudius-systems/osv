#include <map>
#include <list>
#include <errno.h>
#include "drivers/clock.hh"
#include "sched.hh"
#include "osv/trace.hh"

extern "C" {
    #include <bsd/porting/netport.h>
    #include <bsd/porting/synch.h>
    #include <bsd/porting/sync_stub.h>
}

TRACEPOINT(trace_synch_msleep, "chan=%p mtx=%p timo_hz=%d", void *, void *, int);
TRACEPOINT(trace_synch_msleep_wait, "chan=%p", void *);
TRACEPOINT(trace_synch_msleep_timeout_wait, "chan=%p", void *);
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

    int msleep(void *chan, struct mtx *mtx, int priority, const char *wmesg,
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

int synch_port::msleep(void *chan, struct mtx *mtx,
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

    if (timo_hz) {
        u64 nanoseconds = timo_hz*(1000000000L/hz);
        u64 cur_time = clock::get()->time();
        sched::timer t(*sched::thread::current());
        t.set(cur_time + nanoseconds);

        trace_synch_msleep_timeout_wait(chan);
        sched::thread::wait_until(wait_lock, [&] {
            return ( (t.expired()) || (wait._awake) );
        });

        // msleep timeout
        if (!wait._awake) {
            trace_synch_msleep_expired(chan);
            return (EWOULDBLOCK);
        }

    } else {

        trace_synch_msleep_wait(chan);
        sched::thread::wait_until(wait_lock, [&] {
            return (wait._awake);
        });

    }

    return (0);
}

void synch_port::wakeup(void* chan)
{
    trace_synch_wakeup(chan);
    std::list<synch_thread *> ttw;

    mutex_lock(&_lock);
    auto ppp = _evlist.equal_range(chan);

    for (auto it=ppp.first; it!=ppp.second; ++it) {
        synch_thread* wait = (*it).second;
        ttw.push_back(wait);
    }
    _evlist.erase(ppp.first, ppp.second);
    mutex_unlock(&_lock);

    for (auto st: ttw) {
        trace_synch_wakeup_waking(chan, st->_thread);
        st->_awake = true;
        st->_thread->wake();
    }
}

void synch_port::wakeup_one(void* chan)
{
    trace_synch_wakeup_one(chan);
    synch_thread* wait = nullptr;

    mutex_lock(&_lock);
    auto ppp = _evlist.equal_range(chan);
    auto it = ppp.first;
    if (it != _evlist.end()) {
        wait = (*it).second;
        _evlist.erase(it);
    }
    mutex_unlock(&_lock);

    if (wait) {
        trace_synch_wakeup_one_waking(chan, wait->_thread);
        wait->_awake = true;
        wait->_thread->wake();
    }
}

extern "C" int msleep(void *chan, struct mtx *mtx, int priority, const char *wmesg,
     int timo)
{
    return (synch_port::instance()->msleep(chan, mtx, priority, wmesg, timo));
}

extern "C" int tsleep(void *chan, int priority, const char *wmesg, int timo)
{
    return (msleep(chan, 0, priority, wmesg, timo));
}

extern "C" void bsd_pause(const char *wmesg, int timo)
{
    msleep(0, 0, 0, wmesg, timo);
}

extern "C" void wakeup(void* chan)
{
    synch_port::instance()->wakeup(chan);
}

extern "C" void wakeup_one(void* chan)
{
    synch_port::instance()->wakeup_one(chan);
}
