/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <boost/intrusive/list.hpp>
#include <errno.h>
#include <osv/sched.hh>
#include "osv/trace.hh"
#include <osv/export.h>

#include <bsd/porting/netport.h>
#include <bsd/porting/synch.h>
#include <bsd/porting/sync_stub.h>
#include <bsd/sys/sys/param.h>
#include <osv/kernel_config_fork.h>
#if CONF_fork
#include <memory>
#include <osv/align.hh>
#include <osv/fork_arena.hh>
#endif

TRACEPOINT(trace_synch_msleep, "chan=%p mtx=%p timo_hz=%d", void *, void *, int);
TRACEPOINT(trace_synch_msleep_wait, "chan=%p timo_hz=%d", void *, int);
TRACEPOINT(trace_synch_msleep_expired, "chan=%p", void *);
TRACEPOINT(trace_synch_wakeup, "chan=%p", void *);
TRACEPOINT(trace_synch_wakeup_waking, "chan=%p thread=%p", void *, void *);
TRACEPOINT(trace_synch_wakeup_one, "chan=%p", void *);
TRACEPOINT(trace_synch_wakeup_one_waking, "chan=%p thread=%p", void *, void *);

// A sleeper waits on its own stack frame: msleep() links it into the bucket
// its channel hashes to and unlinks it before returning, so nothing is
// allocated per sleep and no per-channel state outlives its last sleeper.
struct synch_thread {
    sched::thread* _thread;
    void* _chan;
    std::atomic<bool> _awake;
    boost::intrusive::list_member_hook<> _hook;
};

// The sleep queue is a fixed table of buckets.  Each bucket has its own lock
// and one FIFO list of the sleepers whose channels hash to it; a wakeup walks
// only its bucket and skips sleepers of other channels.  Channels that share
// a bucket share its lock but never wake each other.
class synch_port {
public:
    int _msleep(void *chan, struct mtx *mtx, int priority, const char *wmesg,
         int timo);

    void wakeup(void* chan);

    void wakeup_one(void* chan);

private:
    using waiter_list = boost::intrusive::list<synch_thread,
        boost::intrusive::member_hook<synch_thread,
            boost::intrusive::list_member_hook<>, &synch_thread::_hook>,
        boost::intrusive::constant_time_size<false>>;

    struct alignas(64) bucket {
        mutex_t lock;
        waiter_list waiters;
    };

    static constexpr unsigned nbuckets = 256;

    bucket& bucket_of(void* chan) {
        // Channels are object addresses: drop the low bits that alignment
        // makes constant, then mix so neighbouring objects spread out.
        auto h = reinterpret_cast<uintptr_t>(chan) >> 4;
        h ^= h >> 9;
        h *= 0x9e3779b97f4a7c15ull;
        return _buckets[h >> (64 - 8)];
    }

    bucket _buckets[nbuckets];
};

static_assert(256 == 1u << 8, "bucket_of() takes the top 8 bits");

#if CONF_fork
namespace {
// A forked process's stack is private to its copy-on-write address space,
// but a wakeup from another address space follows the bucket list into the
// waiter.  So under CONF_fork the waiter must not live on the sleeper's
// stack.  It must not be malloc'ed either: an allocation per sleep is the
// hot-path cost, and a malloc that needs a page can sleep.  Take it from this
// thread's TLS block, which setup_tcb() puts on the identity heap, so it is
// coherent across address spaces (the lfmutex wait_record does the same).
// msleep() does not nest: between linking the waiter and returning it calls
// only mutexes, a timer, tracepoints and the scheduler, none of which sleeps
// through this port.  Two slots cover a page fault in that window that might
// some day sleep here; past them the waiter comes from the kernel heap.
constexpr unsigned tls_waiters = 2;
// Over-aligned TLS would change the kernel PT_TLS alignment that the static
// TLS layout depends on, so keep a raw buffer and align by hand.
__thread char tls_waiter_buf[tls_waiters * sizeof(synch_thread) + alignof(synch_thread)];
__thread unsigned tls_waiters_used;

class sleep_waiter {
    synch_thread* _w;
    bool _tls;
public:
    sleep_waiter() {
        _tls = tls_waiters_used < tls_waiters;
        if (_tls) {
            auto base = align_up(reinterpret_cast<uintptr_t>(tls_waiter_buf),
                                 alignof(synch_thread));
            _w = new (reinterpret_cast<char*>(base) +
                      tls_waiters_used++ * sizeof(synch_thread)) synch_thread;
        } else {
            fork_arena::kernel_heap_scope kh;
            _w = new synch_thread;
        }
    }
    ~sleep_waiter() {
        if (_tls) {
            _w->~synch_thread();
            tls_waiters_used--;
        } else {
            fork_arena::kernel_heap_scope kh;
            delete _w;
        }
    }
    synch_thread& get() { return *_w; }
};
}
#endif

// Constructed on first use, so a sleep or wakeup from an early static
// constructor finds the buckets initialized.  Under CONF_fork the port is a
// kernel object every address space shares, so it must not be allocated in a
// fork arena; a function-local static lives in the kernel image's BSS.
static synch_port& synch()
{
    static synch_port port;
    return port;
}

int synch_port::_msleep(void *chan, struct mtx *mtx,
    int priority, const char *wmesg, int timo_hz)
{
    trace_synch_msleep(chan, mtx, timo_hz);

    mutex_t *wait_lock = nullptr;

    // Init the wait
#if CONF_fork
    sleep_waiter waiter_holder;
    synch_thread& wait = waiter_holder.get();
#else
    synch_thread wait;
#endif
    wait._thread = sched::thread::current();
    wait._chan = chan;
    wait._awake.store(false, std::memory_order_release);

    if (mtx) {
        wait_lock = &mtx->_mutex;
    }

    bucket* b = chan ? &bucket_of(chan) : nullptr;
    if (b) {
        mutex_lock(&b->lock);
        b->waiters.push_back(wait);
        mutex_unlock(&b->lock);
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
                     (wait._awake.load(std::memory_order_acquire)) );

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
    if (!wait._awake.load(std::memory_order_acquire)) {
        trace_synch_msleep_expired(chan);
        if (b) {
            // A waker may have taken us off the list and set _awake after the
            // check above; under the bucket lock the two cannot cross, so
            // decide again there.  Only a waiter still linked has not been
            // woken, and it must leave the list before its frame goes away.
            mutex_lock(&b->lock);
            bool woken = !wait._hook.is_linked();
            if (!woken) {
                b->waiters.erase(b->waiters.iterator_to(wait));
            }
            mutex_unlock(&b->lock);
            if (woken) {
                // The wakeup won the race against the timeout: report it.
                // Returning EWOULDBLOCK here would lose a wakeup_one, since
                // it already took this sleeper instead of another one.
                return (0);
            }
        }
        return interrupted ? (EINTR) : (EWOULDBLOCK);
    }

    // Woken: the waker unlinked this frame and its last access to it was the
    // store to _awake we just saw, so the frame may go away now.
    return (0);
}

void synch_port::wakeup(void* chan)
{
    trace_synch_wakeup(chan);

    bucket& b = bucket_of(chan);
    mutex_lock(&b.lock);
    for (auto it = b.waiters.begin(); it != b.waiters.end(); ) {
        synch_thread* wait = &*it;
        if (wait->_chan != chan) {
            ++it;
            continue;
        }
        it = b.waiters.erase(it);
        trace_synch_wakeup_waking(chan, wait->_thread);
        wait->_thread->wake_with([&] { wait->_awake.store(true, std::memory_order_release); });
    }
    mutex_unlock(&b.lock);
}

void synch_port::wakeup_one(void* chan)
{
    trace_synch_wakeup_one(chan);

    bucket& b = bucket_of(chan);
    mutex_lock(&b.lock);
    for (auto it = b.waiters.begin(); it != b.waiters.end(); ++it) {
        synch_thread* wait = &*it;
        if (wait->_chan != chan) {
            continue;
        }
        b.waiters.erase(it);
        trace_synch_wakeup_one_waking(chan, wait->_thread);
        wait->_thread->wake_with([&] { wait->_awake.store(true, std::memory_order_release); });
        break;
    }
    mutex_unlock(&b.lock);
}

extern "C" OSV_LIBSOLARIS_API int _msleep(void *chan, struct mtx *mtx, int priority, const char *wmesg,
     int timo)
{
    return (synch()._msleep(chan, mtx, priority, wmesg, timo));
}

extern "C" int tsleep(void *chan, int priority, const char *wmesg, int timo)
{
    return (_msleep(chan, 0, priority, wmesg, timo));
}

extern "C" OSV_LIBSOLARIS_API void bsd_pause(const char *wmesg, int timo)
{
    _msleep(0, 0, 0, wmesg, timo);
}

extern "C" OSV_LIBSOLARIS_API void wakeup(void* chan)
{
    synch().wakeup(chan);
}

extern "C" void wakeup_one(void* chan)
{
    synch().wakeup_one(chan);
}
