#include "sched.hh"
#include "debug.hh"

extern "C" {
#include <bsd/porting/netport.h>
#include <bsd/porting/sync_stub.h>

#define _WANT_NETISR_INTERNAL
#include <bsd/sys/net/netisr.h>
#include <bsd/sys/net/netisr_internal.h>
}


static inline sched::thread* niosv_to_thread(netisr_osv_cookie_t cookie)
{
    return (reinterpret_cast<sched::thread*>(cookie));
}

static inline netisr_osv_cookie_t niosv_to_cookie(sched::thread* t)
{
    return (reinterpret_cast<netisr_osv_cookie_t>(t));
}

static int netisr_osv_have_work = 0;
void netisr_osv_thread_wrapper(netisr_osv_handler_t handler, void* arg)
{
    while (1) {
        sched::thread::wait_until([&] { return (netisr_osv_have_work); });

        handler(arg);
        netisr_osv_have_work = 0;
    }
}

/* FIXME
 * Only one thread is created,
 * but we lay here the framework for per-cpu work */
netisr_osv_cookie_t netisr_osv_start_thread(netisr_osv_handler_t handler, void* arg)
{
    sched::thread* t = new sched::thread([=] {
        netisr_osv_thread_wrapper(handler, arg);
    });
    t->start();

    return (niosv_to_cookie(t));
}

void netisr_osv_sched(netisr_osv_cookie_t cookie)
{
    sched::thread* t = niosv_to_thread(cookie);
    netisr_osv_have_work = 1;
    t->wake();

    /* FIXME: Cooperative threads... */
    sched::thread::yield();
}
