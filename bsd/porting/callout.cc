#include <functional>
#include "drivers/clock.hh"
#include "debug.hh"
#include "sched.hh"

extern "C" {
    #include <bsd/porting/netport.h>
    #include <bsd/porting/callout.h>
}

sched::thread* callout_get_thread(struct callout *c)
{
    return reinterpret_cast<sched::thread*>(c->thread);
}

void callout_set_thread(struct callout *c, sched::thread* t)
{
    c->thread = reinterpret_cast<void*>(t);
}


int callout_reset_on(struct callout *c, u64 to_ticks, void (*ftn)(void *),
    void *arg, int ignore_cpu)
{
    u64 nanoseconds = to_ticks;
    int result = 0;

    if ( (c->c_state == CALLOUT_S_SCHEDULED) &&
         (callout_get_thread(c) != sched::thread::current()) ) {

        c->c_stopped = 1;
        callout_get_thread(c)->join();
        result = 1;
    }

    c->c_stopped = 0;
    c->c_state = CALLOUT_S_SCHEDULED;

    sched::thread* callout_thread = new sched::thread([=] {

        sched::timer t(*sched::thread::current());
        t.set(clock::get()->time() + nanoseconds);
        sched::thread::wait_until([&] { return ( (t.expired()) || (c->c_stopped) ); });

        if (c->c_stopped == 0) {
            ftn(arg);
            c->c_state = CALLOUT_S_COMPLETED;
        }
    });

    callout_set_thread(c, callout_thread);

    return (result);
}

int _callout_stop_safe(struct callout *c, int safe)
{
    assert(safe != 0);

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

