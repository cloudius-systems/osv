
#include <errno.h>
#include <stddef.h>
#include <assert.h>
#include "sched.hh"

#include <bsd/porting/netport.h>
#include <bsd/sys/sys/kthread.h>

int
kthread_add(void (*func)(void *), void *arg, struct proc *p,
		struct thread **newtdp, int flags, int pages,
		const char *fmt, ...)
{
    assert(p == NULL);
    assert(flags == 0);
    assert(pages == NULL);

    sched::thread* t = new sched::thread([=] { func(arg); });
    t->start();

    *newtdp = reinterpret_cast<struct thread *>(t);
    return 0;
}

void
kthread_exit(void)
{
    sched::thread::exit();
}
