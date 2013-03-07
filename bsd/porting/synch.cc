#include <map>
#include <errno.h>
#include "debug.hh"
#include "drivers/clock.hh"
#include "sched.hh"

#define SYNCH_LOG(...) \
    logger::instance()->wrt("bsd-synch", logger_error, __VA_ARGS__)

extern "C" {
    #include <bsd/porting/netport.h>
    #include <bsd/porting/synch.h>
    #include <bsd/porting/sync_stub.h>
}

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
    SYNCH_LOG("msleep chan=%x, mtx=%x, timo_seconds=%d", chan, mtx, timo_hz);

    mutex_t *wait_lock = nullptr;

    // Init the wait
    synch_thread wait;
    wait._thread = sched::thread::current();
    wait._awake = false;

    if (mtx) {
        wait_lock = &mtx->_mutex;
    }

    mutex_lock(&_lock);
    _evlist.insert(std::make_pair(chan, &wait));
    mutex_unlock(&_lock);

    if (timo_hz) {
        u64 nanoseconds = timo_hz*(1000000000L/hz);
        u64 cur_time = clock::get()->time();
        sched::timer t(*sched::thread::current());
        t.set(cur_time + nanoseconds);

        sched::thread::wait_until(wait_lock, [&] {
            return ( (t.expired()) || (wait._awake) );
        });

        // msleep timeout
        if (!wait._awake) {
            return (EWOULDBLOCK);
        }

    } else {

        sched::thread::wait_until(wait_lock, [&] {
            return (wait._awake);
        });

    }

    return (0);
}

void synch_port::wakeup(void* chan)
{
    SYNCH_LOG("wakeup chan=%x", chan);
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
        st->_awake = true;
        st->_thread->wake();
    }
}

void synch_port::wakeup_one(void* chan)
{
    synch_thread* wait = nullptr;
    SYNCH_LOG("wakeup_one");

    mutex_lock(&_lock);
    auto ppp = _evlist.equal_range(chan);
    auto it = ppp.first;
    if (it != _evlist.end()) {
        wait = (*it).second;
        _evlist.erase(it);
    }
    mutex_unlock(&_lock);

    if (wait) {
        wait->_awake = true;
        wait->_thread->wake();
    }
}

extern "C" int msleep(void *chan, struct mtx *mtx, int priority, const char *wmesg,
     int timo)
{
    return (synch_port::instance()->msleep(chan, mtx, priority, wmesg, timo));
}

extern "C" void wakeup(void* chan)
{
    synch_port::instance()->wakeup(chan);
}

extern "C" void wakeup_one(void* chan)
{
    synch_port::instance()->wakeup_one(chan);
}
