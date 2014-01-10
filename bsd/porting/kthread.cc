/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <errno.h>
#include <stddef.h>
#include <assert.h>
#include "sched.hh"

#include <bsd/porting/curthread.h>
#include <bsd/porting/netport.h>
#include <bsd/porting/kthread.h>
#include <bsd/sys/sys/kthread.h>

struct proc proc0;

int
kthread_add(void (*func)(void *), void *arg, struct proc *p,
		struct thread **newtdp, int flags, int pages,
		const char *fmt, ...)
{
    assert(p == NULL || p == &proc0);
    assert(flags == 0);
    assert(pages == NULL);

    sched::thread* t = new sched::thread([=] { func(arg); },
            sched::thread::attr().detached());
    t->start();

    *newtdp = reinterpret_cast<struct thread *>(t);
    return 0;
}


int kproc_create(void (*func)(void *), void *arg, struct proc **p,
                int flags, int pages, const char *str, ...)
{
    sched::thread* t = new sched::thread([=] { func(arg); },
            sched::thread::attr().detached());
    t->start();

    if (p) {
        *p = reinterpret_cast<struct proc *>(sched::thread::current());
    }
    return 0;
}

void
kthread_exit(void)
{
    sched::thread::exit();
}

struct thread *
get_curthread(void)
{
    return reinterpret_cast<struct thread *>(sched::thread::current());
}

struct proc *
get_curproc(void)
{
    return reinterpret_cast<struct proc *>(sched::thread::current());
}
