#include "drivers/clock.hh"
#include "debug.hh"
#include "sched.hh"

extern "C" {
    #include <bsd/porting/netport.h>
    #include <bsd/porting/callout.h>
    #include <bsd/porting/rwlock.h>
    #include <bsd/porting/sync_stub.h>
}

#define cdbg(...) logger::instance()->wrt("callout", logger_debug, __VA_ARGS__)

int _callout_stop_safe_locked(struct callout *c, int safe);

sched::detached_thread* callout_get_thread(struct callout *c)
{
    return reinterpret_cast<sched::detached_thread*>(c->thread);
}

void callout_set_thread(struct callout *c, sched::detached_thread* t)
{
    c->thread = reinterpret_cast<void*>(t);
}

sched::thread* callout_get_waiter(struct callout *c)
{
    return reinterpret_cast<sched::thread*>(c->waiter_thread);
}

void callout_set_waiter(struct callout *c, sched::thread* t)
{
    if (t != NULL)
        assert(c->waiter_thread == NULL);

    c->waiter_thread = reinterpret_cast<void*>(t);
}

void _callout_wrapper(struct callout *c)
{
    sched::thread* current = sched::thread::current();

    CALLOUT_LOCK(c);

    cdbg("*C* C=0x%lx _callout_wrapper()",
        (uint64_t)c, c->c_stopped);

    if (c->c_stopped) {
        cdbg("*C* C=0x%lx _callout_wrapper() stopped 1", (uint64_t)c);
        goto completed;
    }

    assert(c->c_flags & (CALLOUT_ACTIVE | CALLOUT_PENDING));

    do {

        //////////////////////
        // Wait for timeout //
        //////////////////////

        c->c_reschedule = 0;
        c->c_flags |= CALLOUT_PENDING;

        cdbg("*C* C=0x%lx _callout_wrapper() pending", (uint64_t)c);

        uint64_t cur = clock::get()->time();
        if (cur < c->c_to_ns+TMILISECOND) {
            sched::timer t(*sched::detached_thread::current());
            t.set(c->c_to_ns);
            sched::detached_thread::wait_until(c->c_callout_mtx, [&] {
                return ( (t.expired()) || (c->c_stopped) );
            });
        }

        if (c->c_stopped == 1) {
            cdbg("*C* C=0x%lx _callout_wrapper() stopped 2", (uint64_t)c);
            goto completed;
        }

        struct mtx* c_mtx = c->c_mtx;
        struct rwlock* c_rwlock = c->c_rwlock;
        bool return_unlocked = ((c->c_flags & CALLOUT_RETURNUNLOCKED) == 0);

        ///////////////
        // Dispatch! //
        ///////////////

        if (c_rwlock)
            rw_wlock(c_rwlock);
        if (c_mtx)
            mtx_lock(c_mtx);

        c->c_flags &= ~CALLOUT_PENDING;
        c->c_flags |= CALLOUT_DISPATCHING;
        CALLOUT_UNLOCK(c);

        // Callout handler
        c->c_fn(c->c_arg);

        CALLOUT_LOCK(c);

        if (return_unlocked) {
            if (c_rwlock)
                rw_wunlock(c_rwlock);
            if (c_mtx)
                mtx_unlock(c_mtx);
        }

        // Check if we are still the owner of this callout
        // (we don't if callout_stop() was called and we were dispatching)
        sched::thread* detached = callout_get_thread(c);
        if (detached != current) {
            c->c_stopped = 0;
            cdbg("*C* C=0x%lx _callout_wrapper() lost ownership to 0x%lx",
                (uint64_t)c, (uint64_t)detached);
            CALLOUT_UNLOCK(c);
            return;
        }

        c->c_flags &= ~CALLOUT_DISPATCHING;

    } while (c->c_reschedule && !c->c_stopped);

completed:

    cdbg("*C* C=0x%lx _callout_wrapper() completed",
        (uint64_t)c, c->c_stopped);

    c->c_stopped = 0;
    c->c_reschedule = 0;
    c->c_flags &= ~(CALLOUT_PENDING | CALLOUT_DISPATCHING);
    c->c_flags |= CALLOUT_COMPLETED;
    sched::thread* waiter = callout_get_waiter(c);
    callout_set_waiter(c, NULL);
    if (waiter) {
        cdbg("*C* C=0x%lx _callout_wrapper() waking 0x%lx",
            (uint64_t)c, waiter);
        waiter->wake();
    }
    CALLOUT_UNLOCK(c);
}

int callout_reset_on(struct callout *c, u64 to_ticks, void (*fn)(void *),
    void *arg, int ignore_cpu)
{
    sched::thread *orig_thread = callout_get_thread(c);
    sched::thread *current_thread = sched::thread::current();
    u64 cur = clock::get()->time();
    int cur_ticks = ns2ticks(cur);
    int result = 0;

    CALLOUT_LOCK(c);

    cdbg("*C* C=0x%lx callout_reset_on() to_ticks=%lu fn=0x%lx arg=0x%lx",
        (uint64_t)c, to_ticks, fn, arg);

    if ((c->c_flags & CALLOUT_DISPATCHING) &&
        (orig_thread == current_thread) &&
        (c->c_stopped == 1)) {
        cdbg("*C* C=0x%lx callout_reset_on() callout stopped", (uint64_t)c);
        CALLOUT_UNLOCK(c);

        return result;
    }

    // If callout is scheduled
    if ( (c->c_flags & CALLOUT_PENDING) ||
         ((c->c_flags & CALLOUT_DISPATCHING) &&
          (orig_thread != current_thread)) ) {
        _callout_stop_safe_locked(c, 1);
        result = 1;
    }

    void (*old_fn)(void *) = c->c_fn;
    void* old_arg = c->c_arg;

    // Reset the callout
    c->c_stopped = 0;
    c->c_reschedule = 0;
    c->c_time = cur_ticks + to_ticks;
    c->c_to_ns = cur + ticks2ns(to_ticks);
    c->c_fn = fn;
    c->c_arg = arg;

    // Check for recursive callouts
    if ((c->c_flags & CALLOUT_DISPATCHING) &&
        (orig_thread == current_thread)) {

        assert(old_fn == fn);
        assert(old_arg == arg);

        c->c_reschedule = 1;

        cdbg("*C* C=0x%lx callout_reset_on() rescheduling...", (uint64_t)c);

        CALLOUT_UNLOCK(c);
        return 1;
    }

    sched::detached_thread* callout_thread = new sched::detached_thread([=] {
        _callout_wrapper(c);
    });

    callout_set_thread(c, callout_thread);
    c->c_flags &= ~(CALLOUT_COMPLETED | CALLOUT_DISPATCHING);
    c->c_flags |= (CALLOUT_PENDING | CALLOUT_ACTIVE);
    CALLOUT_UNLOCK(c);

    callout_thread->start();
    return result;
}

/*
 * callout_stop() and callout_drain()
 */
int _callout_stop_safe_locked(struct callout *c, int safe)
{
    sched::detached_thread* callout_thread = callout_get_thread(c);
    cdbg("*C* C=0x%lx _callout_stop_safe_locked() c->thread=0x%lx c->flags=0x%04x",
        (uint64_t)c, (uint64_t)callout_thread, c->c_flags);
    if (!callout_thread) {
        c->c_flags &= ~(CALLOUT_ACTIVE | CALLOUT_PENDING);
        return 0;
    }

    c->c_stopped = 1;
    c->c_reschedule = 0;

    if (c->c_flags & CALLOUT_PENDING) {
        cdbg("*C* C=0x%lx _callout_stop_safe_locked() waiting...", (uint64_t)c);

        assert(callout_thread != sched::thread::current());
        callout_set_waiter(c, sched::thread::current());
        callout_thread->wake();
        sched::thread::wait_until(c->c_callout_mtx, [&] {
            return (c->c_flags & CALLOUT_COMPLETED);
        });

        cdbg("*C* C=0x%lx _callout_stop_safe_locked() stopped waiting",
            (uint64_t)c);
    }

    // Clear flags
    c->c_flags &= ~(CALLOUT_ACTIVE | CALLOUT_PENDING | CALLOUT_COMPLETED);
    callout_set_waiter(c, NULL);
    callout_set_thread(c, NULL);

    return 0;
}

int _callout_stop_safe(struct callout *c, int safe)
{
    int result = 0;
    CALLOUT_LOCK(c);
    result = _callout_stop_safe_locked(c, safe);
    CALLOUT_UNLOCK(c);

    return (result);
}

/*
 * If mpsafe is zero, the callout should wrap the call to the handler
 * In freebsd, this is done using the Giant lock.
 */
void callout_init(struct callout *c, int mpsafe)
{
    bzero(c, sizeof *c);
    assert(mpsafe != 0);

    cdbg("*C* C=0x%lx callout_init()", (uint64_t)c);

    mutex_init(&c->c_callout_mtx);
}

void callout_init_rw(struct callout *c, struct rwlock *rw, int flags)
{
    assert(rw != NULL);

    callout_init(c, 1);
    c->c_rwlock = rw;
    c->c_flags = flags;
}

void callout_init_mtx(struct callout *c, struct mtx *mtx, int flags)
{
    assert(mtx != NULL);

    callout_init(c, 1);
    c->c_mtx = mtx;
    c->c_flags = flags;
}
