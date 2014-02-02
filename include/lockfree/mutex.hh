/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef LOCKFREE_MUTEX_HH
#define LOCKFREE_MUTEX_HH
// A lock-free mutex implementation, based on the combination of two basic
// techniques:
// 1. Our lock-free multi-producer single-consumer queue technique
//    (see lockfree/queue-mpsc.hh)
// 2. The "responsibility hand-off" (RHO) protocol described in the 2007 paper
//    "Blocking without Locking or LFTHREADS: A lock-free thread library"
//    by Anders Gidenstam and Marina Papatriantafilou.
//
// The operation and correctness of the RHO protocol is discussed in the
// aforementioned G&P 2007 paper, so we will avoid lengthy comments about it
// below, except where we differ from G&P.
//
// One especially important issue that we do need to justify is:
// Our lockfree queue implementation assumes that there cannot be two
// concurrent pop()s. We claim that this is true in the RHO protocol because:
// 1. We have pop() calls at two places:
//    (A) In unlock(), after decrementing count and outside a handoff (=null)
//    (B) in lock(), after picking up a handoff.
// 2. We can't have two threads at (A) at the same time, because one thread
//    at (A) means another thread thread was just in lock() (because count>0),
//    but currently running lock()s cannot complete (and get to unlock and A)
//    until somebody will wake them it (and this is what we're trying to show
//    is impossible), and news lock()s will likewise wait because the waiting
//    lock() is keeping count>0.
// 3. While one lock() is at (B), we cannot have another thread at (A) or (B):
//    This is because in (B) we only pop() after picking a handoff, so other
//    lock()s cannot reach (B) (they did not pick the handoff, we did), and
//    unlock cannot be at (A) because it only reaches (A) before making the
//    handoff of after taking it back - and we know it didn't because we took
//    the handoff.
//
// Another difference from our implementation from G&P is the content of the
// handoff token. G&P use the processor ID, but remark that it is not enough
// because of the ABA problem (it is possible while a CPU running lock() is
// paused, another one finishes unlock(), and then succeeds in another lock()
// and then comes a different unlock() with its unrelated handoff) and suggest
// to add a per-processor sequence number. Instead, we just used a per-mutex
// sequence number. As long as one CPU does not pause for a long enough
// duration for our (currently 32-bit) sequence number to wrap, we won't have
// a problem. A per-mutex sequence number is slower than a per-cpu one, but
// I doubt this will make a practical difference.

#include <atomic>
#include <lockfree/queue-mpsc.hh>

// we don't want to include <sched.hh> because that includes a bunch of things
// which eventually, recursively, use mutexes.
// We also can't include <osv/wait_record.hh>, as that includes <sched.hh>.
namespace sched {
    class thread;
}
struct wait_record;

namespace lockfree {

class mutex {
protected:
    std::atomic<int> count;
    // "owner" and "depth" are only used for implementing a recursive mutex.
    // "depth" is not an atomic variable - only the lock-owning thread sets
    // and reads its own depth. "owner" is atomic - one thread doing lock()
    // needs to read the current owner possibly set by another thread - but
    // it can be accessed with relaxed memory ordering.
    unsigned int depth;
    std::atomic<sched::thread *> owner;
    queue_mpsc<wait_record> waitqueue;
    std::atomic<unsigned int> handoff;
    unsigned int sequence;
public:
    // Note: mutex's constructor just initializes the whole structure to
    // zero, and its destructor does nothing. This is useful to know when
    // allocating a mutex in C.
    constexpr mutex() : count(0), depth(0), owner(nullptr), waitqueue(), handoff(0), sequence(0) { }
    ~mutex() { /*assert(count==0);*/ }

    void lock();
    bool try_lock();
    void unlock();

    bool owned() const;
    // getdepth() should only be used by the thread holding the lock
    inline unsigned int getdepth() const { return depth; }

    // For wait morphing. Do not use unless you know what you are doing :-)
    void send_lock(wait_record *wr);
    bool send_lock_unless_already_waiting(wait_record *wr);
    void receive_lock();
};

}
#endif
