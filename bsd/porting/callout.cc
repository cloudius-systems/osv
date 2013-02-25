#include <functional>
#include "drivers/clock.hh"
#include "debug.hh"
#include "sched.hh"

extern "C" {
    #include <bsd/porting/netport.h>
    #include <bsd/porting/callout.h>
    #include <bsd/porting/rwlock.h>

}

sched::thread* callout_get_thread(struct callout *c)
{
    return reinterpret_cast<sched::detached_thread*>(c->thread);
}

void callout_set_thread(struct callout *c, sched::thread* t)
{
    c->thread = reinterpret_cast<void*>(t);
}


int callout_reset_on(struct callout *c, u64 to_ticks, void (*ftn)(void *),
    void *arg, int ignore_cpu)
{
    u64 nanoseconds = to_ticks;
    u64 cur_time = clock::get()->time();
    int result = 0;

    if ( (c->c_state == CALLOUT_S_SCHEDULED) &&
         (callout_get_thread(c) != sched::detached_thread::current()) ) {

        c->c_stopped = 1;
        callout_get_thread(c)->join();
        result = 1;
    }

    c->c_stopped = 0;
    c->c_state = CALLOUT_S_SCHEDULED;

    // FIXME: Free this thread
    sched::thread* callout_thread = new sched::detached_thread([=] {

        sched::timer t(*sched::thread::current());
        t.set(cur_time + nanoseconds);
        sched::thread::wait_until([&] { return ( (t.expired()) || (c->c_stopped) ); });

        if (c->c_stopped == 0) {
            if (c->c_is_rwlock == 1) {
                rw_wlock(c->c_rwlock);
            }

            // Callout
            ftn(arg);

            if ((c->c_flags & CALLOUT_RETURNUNLOCKED) == 0) {
                if (c->c_is_rwlock == 1) {
                    rw_wunlock(c->c_rwlock);
                }
            }
            c->c_state = CALLOUT_S_COMPLETED;
        }
    });

    callout_set_thread(c, callout_thread);

    return (result);
}

int _callout_stop_safe(struct callout *c, int safe)
{
    // FIXME: handle safe != 0 properly...
    // assert(safe != 0);

    c->c_stopped = 1;
    sched::thread* callout_thread = callout_get_thread(c);

    if (callout_thread) {
        callout_thread->wake();
    }

    return 1;
}

/*
 * If mpsafe is zero, the callout interface should protect the structure
 * In feebsd, this is done using the Giant lock.
 */
void callout_init(struct callout *c, int mpsafe)
{
    bzero(c, sizeof *c);
    assert(mpsafe != 0);

    c->c_flags = CALLOUT_RETURNUNLOCKED;
    c->c_state = CALLOUT_S_IDLE;
    c->c_stopped = 0;
}

void callout_init_rw(struct callout *c, struct rwlock *rw, int flags)
{
    assert(rw != NULL);

    callout_init(c, 1);
    c->c_is_rwlock = 1;
    c->c_flags = flags;
}
