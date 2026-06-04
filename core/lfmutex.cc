/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <lockfree/mutex.hh>
#include <osv/trace.hh>
#include <osv/sched.hh>
#include <osv/wait_record.hh>
#include <osv/clock.hh>
#include <osv/export.h>

namespace lockfree {

TRACEPOINT(trace_mutex_lock, "%p", mutex *);
TRACEPOINT(trace_mutex_lock_wait, "%p", mutex *);
TRACEPOINT(trace_mutex_lock_wake, "%p", mutex *);
TRACEPOINT(trace_mutex_try_lock, "%p, success=%d", mutex *, bool);
TRACEPOINT(trace_mutex_unlock, "%p", mutex *);
TRACEPOINT(trace_mutex_send_lock, "%p, wr=%p", mutex *, wait_record *);
TRACEPOINT(trace_mutex_receive_lock, "%p", mutex *);
TRACEPOINT(trace_mutex_spin_success, "%p, attempt=%d, count=%d, spun=%d, spin_time=%ld, fholder=%p, lholder=%p",
    mutex*, int, int, int, u64, sched::thread*, sched::thread*);
TRACEPOINT(trace_mutex_spin_failure, "%p, attempt=%d, count=%d, spun=%d, spin_time=%ld, fholder=%p, lholder=%p",
    mutex*, int, int, int, u64, sched::thread*, sched::thread*);

//With new change to stop spinning after failed handoff, the 20 seems to be sweet spot
constexpr unsigned int spin_max = 20; //20 Seems best, 10 is kind of on a line

#define CONF_mutex_preempt_off    0 //Does not seem to change much
#define CONF_mutex_spin_stop_when_holder_null 0
#define CONF_mutex_spin_attempt_1 1
#define CONF_mutex_spin_attempt_2 0 //Seems to improve misc-mutex -c with 3 threads better, but with 2 threads
                                    //worse than when off (still better than without spinning)
#define CONF_mutex_spin_attempt_2_if_waitqueue_empty 0
#define CONF_mutex_wake_set_owner 0 //Makes misc-ctx colocated run almost unaffected if 1 (ON) but greatly diminishes
                                    //effects of spinning just like spin_stop_when_holder_null does

#if CONF_mutex_preempt_off
#include <osv/preempt-lock.hh>
#endif

void mutex::lock()
{
    trace_mutex_lock(this);

    sched::thread *current = sched::thread::current();

#if CONF_mutex_preempt_off
    preempt_lock.lock(); //Should we disable preemption just before spinning?
#endif
    auto _count = count.fetch_add(1, std::memory_order_acquire);
    if (_count == 0) {
        // Uncontended case (no other thread is holding the lock, and no
        // concurrent lock() attempts). We got the lock.
        // Setting count=1 already got us the lock; we set owner and depth
        // just for implementing a recursive mutex.
        owner.store(current, std::memory_order_relaxed);
        depth = 1;
#if CONF_mutex_preempt_off
        preempt_lock.unlock();
#endif
        return;
    }

    // If we're here the mutex was already locked, but we're implementing
    // a recursive mutex so it's possible the lock holder is us - in which
    // case we need to increment depth instead of waiting.
    if (owner.load(std::memory_order_relaxed) == current) {
        count.fetch_add(-1, std::memory_order_relaxed);
        ++depth;
#if CONF_mutex_preempt_off
        preempt_lock.unlock();
#endif
        return;
    }

    // If we're still here, the lock is owned by a different thread. Before
    // going to sleep, let's try to spin for a bit, to avoid the context
    // switch costs when the critical section is very short. We're spinning
    // without putting ourselves in the waitqueue first, so when the lock
    // holder unlock()s, we expect to see a handoff.
    //
    // wkozaczuk: number of spinning iterations could be passed as a constructor
    // parameter - the default could be 0 or a constant that can be changed
    // as a kernel build parameter (the default constant value would be 0
    // - 'spinning off'). Hopefully, default 0 would not change current behavior.
    // Also the constructor should always ignore the value if #cpus = 1 and set it to 0
    // Also consider build parameter to enable/disable mutex spinning
    // We could change rwlock to construct mutex with the spin parameter > 0 (=10?)
    // Alternative would be to create a template
    //
    // If someone is on the waitqueue, there's no point in continuing to
    // spin, as it will be woken first, and no handoff will be done.
#if CONF_mutex_spin_attempt_1
    auto t = clock::get()->time();
    sched::thread* holder = nullptr, *fholder = nullptr;
    unsigned int c = 0, spin_count = sched::cpus.size() > 1 ? spin_max : 0; //do not spin when single CPU
    bool waitqueue_empty;
    for (; c < spin_count; c++) {
        waitqueue_empty = waitqueue.empty();
        if (!waitqueue_empty) {
            break;
        }
        // If the lock holder got preempted, we would better be served
        // by going to sleep and let it run again, than futile spinning.
        // Especially if the lock holder wants to run on this CPU.
        barrier(); // trying. didn't help
        holder = owner.load(std::memory_order_relaxed);
        if (!c) {
            fholder = holder;
        }
        // FIXME: what if thread exits while we want to check if it's
        // running? as soon as we find the owner above, it might already
        // be gone... Switch to using detached_thread as owner?
        // wkozaczuk: the running() method is using _detached_state so are we OK?
        // How exactly _detached_state would help us in the case the thread is terminated
        //
        // wkozaczuk: This probably should be: if (holder && !holder->running()) {
        // as it was in Nadav's version 2 (see https://groups.google.com/g/osv-dev/c/F9fYWjdFmks/m/avafVdCEcXYJ)
        // meaning if there is still a lock holder and got preempted
        // then do not spin. In the original version we would not attempt to spin
        // if there was NOT a lock holder which is exactly when we should spin -
        // see the unlock() where it sets owner to null before trying RHO
#if CONF_mutex_spin_stop_when_holder_null
        if (!holder || !holder->running()) { //SEEMS to be wrong
        // Maybe the above version of "if" would deal better with morhing behavior
        // when "colocated" increases
#else
        if (holder && !holder->running()) {
#endif
            //Either unlock() just set owner to null or owner thread is not running anymore
            break;
        }
        // If the lock holder is on the same CPU as us, better go to sleep
        // and let it run than to futily spin
        //
        // wkozaczuk: I think this is never possible - the above was false, so this code is running
        // on this cpu so there could not be an owner (if there is one)
        // on the same cpu that is also running
        /*if (holder && holder->tcpu() == sched::cpu::current()) {
            break;
        }*/
        // Please note that other threads spinning in lock() that came later
        // may be more lucky than us and grab the handoff - is this unfair?
        auto old_handoff = handoff.load();
        if (old_handoff) {
            if (handoff.compare_exchange_strong(old_handoff, 0U)) {
                owner.store(current, std::memory_order_relaxed);
                depth = 1;
                trace_mutex_spin_success(this, 1, _count, c, clock::get()->time() - t, fholder, holder);
#if CONF_mutex_preempt_off
                preempt_lock.unlock();
#endif
                return;
            } else {
                // Other spinning lock() won - we assume there is no point in more spinning
                // because it would unlikely catch subsequent unlock()
                break;
            }
        }
//TODO: Eventually add inline private pause() method
#ifdef __x86_64__
        __asm __volatile("pause");
#endif
#ifdef __aarch64__
        __asm __volatile("isb sy");
#endif
    }
    if (c > 0)
        trace_mutex_spin_failure(this, 1, _count, c, clock::get()->time() - t, fholder, holder);
#if CONF_mutex_preempt_off
    preempt_lock.unlock();
#endif
#endif

    // If we're here still here the lock is owned by a different thread.
    // Put this thread in a waiting queue, so it will eventually be woken
    // when another thread releases the lock.
    // Note "waiter" is on the stack, so we must not return before making sure
    // it was popped from waitqueue (by another thread or by us.)
    wait_record waiter(current);
    waitqueue.push(&waiter);

    // The "Responsibility Hand-Off" protocol where a lock() picks from
    // a concurrent unlock() the responsibility of waking somebody up:
    auto old_handoff = handoff.load();
    if (old_handoff) {
         if (!waitqueue.empty()){
            if (handoff.compare_exchange_strong(old_handoff, 0U)) {
                // Note the explanation above about no concurrent pop()s also
                // explains why we can be sure waitqueue is still not empty.
                wait_record *other = waitqueue.pop();
                assert(other);
                if (other->thread() != current) {
                    // At this point, waiter.thread() must be != 0, otherwise
                    // it means someone has already woken us up, breaking the
                    // handoff protocol which decided we should be the ones to
                    // wake somebody up. Note that right after other->wake()
                    // below, waiter.thread() may become 0: the thread we woke
                    // can call unlock() and decide to wake us up.
                    assert(waiter.thread());
#if CONF_mutex_wake_set_owner
                    owner.store(const_cast<sched::thread*>(waiter.thread()), std::memory_order_relaxed);
                    depth = 1;
#endif
                    other->wake();
                } else {
                    // got the lock ourselves
                    assert(other == &waiter);
                    owner.store(current, std::memory_order_relaxed);
                    depth = 1;
                    return;
                }
            }
        }
    }

    //Spin again for a possibility that the unlock() popped us from the waitqueue
    //and trying to wake us. If the unlock called wake() in wait_record
    //we can simply try to check if it is woken and prevent us from going to sleep
#if CONF_mutex_spin_attempt_2
    t = clock::get()->time();
#if CONF_mutex_spin_attempt_2_if_waitqueue_empty
    if (waitqueue_empty) {
        spin_count *= 2;
    } else {
        spin_count = 0;
    }
#else
    spin_count *= 2;
#endif
#if CONF_mutex_preempt_off
    preempt_lock.lock();
#endif
    for (c = 0; c < spin_count; c++) {
        barrier(); // trying. didn't help
        holder = owner.load(std::memory_order_relaxed);
        if (!c) {
            fholder = holder;
        }
#if CONF_mutex_spin_stop_when_holder_null
        if (!holder || !holder->running()) {
#else
        if (holder && !holder->running()) {
#endif
            break;
        }
        // Should we move this "if" before checking for holder?
        if (waiter.woken()) {
            owner.store(current, std::memory_order_relaxed);
            depth = 1;
            trace_mutex_spin_success(this, 2, _count, c, clock::get()->time() - t, fholder, holder);
#if CONF_mutex_preempt_off
            preempt_lock.unlock();
#endif
            return;
        }
#ifdef __x86_64__
        __asm __volatile("pause");
#endif
#ifdef __aarch64__
        __asm __volatile("isb sy");
#endif
    }
    if (c > 0)
        trace_mutex_spin_failure(this, 2, _count, c, clock::get()->time() - t, fholder, holder);
#if CONF_mutex_preempt_off
    preempt_lock.unlock();
#endif
#endif

    // Wait until another thread pops us from the wait queue and wakes us up.
    trace_mutex_lock_wait(this);
    waiter.wait();
    trace_mutex_lock_wake(this);
    owner.store(current, std::memory_order_relaxed);
    depth = 1;
}

// send_lock() is used for implementing a "wait morphing" technique, where
// the wait_record of a thread currently waiting on a condvar is put to wait
// on a mutex, without waking it up first. This avoids unnecessary context
// switches that happen if the thread is woken up just to try and grab the
// mutex and go to sleep again.
//
// send_lock(wait_record) is similar to lock(), but instead of taking the lock
// for the current thread, it takes the lock for another thread which is
// currently waiting on the given wait_record. This wait_record will be woken
// when the lock becomes available - either during the send_lock() call, or
// sometime later when the lock holder unlock()s it. It is assumed that the
// waiting thread DOES NOT hold the mutex at the time of this call, so the
// thread should relinquish the lock before putting itself on wait_record.
void mutex::send_lock(wait_record *wr)
{
    trace_mutex_send_lock(this, wr);
    if (count.fetch_add(1, std::memory_order_acquire) == 0) {
        // Uncontended case (no other thread is holding the lock, and no
        // concurrent lock() attempts). We got the lock for wr, so wake it.
#if CONF_mutex_wake_set_owner
        owner.store(const_cast<sched::thread*>(wr->thread()), std::memory_order_relaxed);
        depth = 1;
#endif
        wr->wake();
        return;
    }

    // If we can't grab the lock for wr now, we push it in the wait queue,
    // so it will eventually be woken when another thread releases the lock.
    waitqueue.push(wr);

    // Like in lock(), we incremented "count" before pushing anything on to
    // the queue so a concurrent unlock() may not have found anybody to wake.
    // So we must also do the "Responsibility Hand-Off" protocol to help the
    // concurrent unlock() return without waiting for us to push.
    auto old_handoff = handoff.load();
    if (old_handoff) {
        if (!waitqueue.empty()){
            if (handoff.compare_exchange_strong(old_handoff, 0U)) {
                wait_record *other = waitqueue.pop();
                assert(other);
#if CONF_mutex_wake_set_owner
                owner.store(const_cast<sched::thread*>(other->thread()), std::memory_order_relaxed);
                depth = 1;
#endif
                other->wake();
            }
        }
    }
}

// A variant of send_lock() with the following differences:
//  - the mutex must be held by the caller
//  - we do nothing if the thread we're sending the lock to is already
//    waiting (and return false)
bool mutex::send_lock_unless_already_waiting(wait_record *wr)
{
    trace_mutex_send_lock(this, wr);
    assert(owned());
    // count could not have been zero, so no need to test for it
    count.fetch_add(1, std::memory_order_acquire);

    for (auto& x : waitqueue) {
        if (wr->thread() == x.thread()) {
            return false;
        }
    }
    // Queue the wait record, to be woken by an eventual unlock().
    waitqueue.push(wr);

    // No need for the Responsibility Hand-Off protocol, since we're holding
    // the lock - no concurrent unlock can be happening.

    return true;
}



// A thread waking up knowing it received a lock from send_lock() as part of
// a "wait morphing" protocol, must call receive_lock() to complete the lock's
// adoption by this thread.
// TODO: It is unfortunate that receive_lock() needs to exist at all. If we
// changed the code so that an unlock() always sets owner and depth for the
// thread it wakes, we wouldn't need this function.
void mutex::receive_lock()
{
    trace_mutex_receive_lock(this);
    owner.store(sched::thread::current(), std::memory_order_relaxed);
    depth = 1;
}

bool mutex::try_lock()
{
    sched::thread *current = sched::thread::current();
    int zero = 0;
    if (count.compare_exchange_strong(zero, 1, std::memory_order_acquire)) {
        // Uncontended case. We got the lock.
        owner.store(current, std::memory_order_relaxed);
        depth = 1;
        trace_mutex_try_lock(this, true);
        return true;
    }

    // We're implementing a recursive mutex -lock may still succeed if
    // this thread is the one holding it.
    if (owner.load(std::memory_order_relaxed) == current) {
        ++depth;
        trace_mutex_try_lock(this, true);
        return true;
    }

    // The lock is taken, and we're almost ready to give up (return
    // false), but the last chance is if we can accept a handoff - and if
    // we do, we got the lock.
    auto old_handoff = handoff.load();
    // TODO: Why are we not checking the waitqueue? What if it is not
    // empty, if that case the try_lock() would succeed and bypass the queue.
    // Is that correct?
    // It may be correct, because unlock() which initiates the HO, only does
    // it [aka stores new handoff value], if the waitqueue is empty.
    if(old_handoff && handoff.compare_exchange_strong(old_handoff, 0U)) {
        count.fetch_add(1, std::memory_order_relaxed);
        owner.store(current, std::memory_order_relaxed);
        depth = 1;
        trace_mutex_try_lock(this, true);
        return true;
    }

    trace_mutex_try_lock(this, false);
    return false;
}

void mutex::unlock()
{
    trace_mutex_unlock(this);

    // We assume unlock() is only ever called when this thread is holding
    // the lock. The following assertions don't seem to add any measurable
    // performance penalty, so we leave them in.
    assert(owner.load(std::memory_order_relaxed) == sched::thread::current());
    assert(depth!=0);
    if (--depth)
        return; // recursive mutex still locked.

    // Disable preemption before we set owner=null, so that we don't context
    // switch to a different thread who might end up spinning on this mutex.
    // Alternatives to this change include remembering the lock holder's cpu
    // (makes the mutex larger...), stopping spinning when !holder (makes
    // "apart" ctxsw benchmark slower).
#if CONF_mutex_preempt_off
    SCOPE_LOCK(preempt_lock);
#endif

    // When we return from unlock(), we will no longer be holding the lock.
    // We can't leave owner==current, otherwise a later lock() in the same
    // thread will think it's a recursive lock, while actually another thread
    // is in the middle of acquiring the lock and has set count>0.
    owner.store(nullptr, std::memory_order_relaxed);

    // If there is no waiting lock(), we're done. This is the easy case :-)
    if (count.fetch_add(-1, std::memory_order_release) == 1) {
        return;
    }

    // Otherwise there is at least one concurrent lock(). Awaken one if
    // it's waiting on the waitqueue, otherwise use the RHO protocol to
    // have the lock() responsible for waking someone up.
    // TODO: it's not completely clear to me why more than two
    // iterations can ever be needed. If the loop continues it means
    // another lock() queued itself - but if it did, wouldn't the next
    // iteration just pop and return?
    while(true) {
        wait_record *other = waitqueue.pop();
        if (other) {
            assert(other->thread() != sched::thread::current()); // this thread isn't waiting, we know that :(
#if CONF_mutex_wake_set_owner
            //Setting owner here makes the misc-ctxsw colocated run similar to non-spinning (around 340ns)
            owner.store(const_cast<sched::thread*>(other->thread()), std::memory_order_relaxed);
            depth = 1;
#endif
            other->wake();
            return;
        }
        // Some concurrent lock() is in progress (we know this because of
        // count) but it hasn't yet put itself on the wait queue.
        if (++sequence == 0U) ++sequence;  // pick a number, but not 0
        auto ourhandoff = sequence;
        handoff.store(ourhandoff);
        // If the queue is empty, the concurrent lock() is before adding
        // itself, and therefore will definitely find our handoff later.
        if (waitqueue.empty())
            return;
        // A thread already appeared on the queue, let's try to take the
        // handoff ourselves and awaken it. If somebody else already took
        // the handoff, great, we're done - they are responsible now.
        if (!handoff.compare_exchange_strong(ourhandoff, 0U))
            return;
    }
}

bool mutex::owned() const
{
    return owner.load(std::memory_order_relaxed) == sched::thread::current();
}

sched::thread *mutex::get_owner()
{
    return owner.load(std::memory_order_relaxed);
}
}

// For use in C, which can't access namespaces or methods. Note that for
// performance, we use "flatten" which basically creates a copy of each
// function.
// TODO: We can avoid this copy by make these C functions the only copy of the
// functions, and make the methods inline, calling these functions with
// "this" as a parameter. But this would be un-C++-like :(
extern "C" {
OSV_LIBSOLARIS_API __attribute__((flatten)) void lockfree_mutex_lock(void *m) {
    static_cast<lockfree::mutex *>(m)->lock();
}
OSV_LIBSOLARIS_API __attribute__ ((flatten)) void lockfree_mutex_unlock(void *m) {
    static_cast<lockfree::mutex *>(m)->unlock();
}
OSV_LIBSOLARIS_API __attribute__ ((flatten)) bool lockfree_mutex_try_lock(void *m) {
    return static_cast<lockfree::mutex *>(m)->try_lock();
}
OSV_LIBSOLARIS_API __attribute__ ((flatten)) bool lockfree_mutex_owned(void *m) {
    return static_cast<lockfree::mutex *>(m)->owned();
}
}
