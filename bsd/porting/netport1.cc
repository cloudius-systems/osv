/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/types.h>
#include <osv/sched.hh>
#include <osv/mempool.hh>
#include "bsd/sys/cddl/compat/opensolaris/sys/kcondvar.h"
#include "bsd/cddl/compat/opensolaris/include/mnttab.h"
#include <osv/mount.h>

extern "C" {
    #include <time.h>
    #include <bsd/porting/netport.h>

    int tick = 1;
}

int osv_curtid(void)
{
    return (sched::thread::current()->id());
}

static void ntm2tv(u64 ntm, struct timeval *tvp)
{
    u64 utm = ntm / 1000L;

    tvp->tv_sec = ntm/TSECOND;
    tvp->tv_usec = (utm - (tvp->tv_sec*TMILISECOND));
}

void getmicrotime(struct timeval *tvp)
{
    u64 ntm = std::chrono::duration_cast<std::chrono::nanoseconds>
                (osv::clock::wall::now().time_since_epoch()).count();
    ntm2tv(ntm, tvp);
}

void getmicrouptime(struct timeval *tvp)
{
    u64 ntm = std::chrono::duration_cast<std::chrono::nanoseconds>
                (osv::clock::uptime::now().time_since_epoch()).count();
    ntm2tv(ntm, tvp);
}

int get_ticks(void)
{
    u64 ntm = std::chrono::duration_cast<std::chrono::nanoseconds>
                (osv::clock::uptime::now().time_since_epoch()).count();
    return (ns2ticks(ntm));
}

size_t get_physmem(void)
{
    return memory::phys_mem_size / memory::page_size;
}

int cv_timedwait(kcondvar_t *cv, mutex_t *mutex, clock_t tmo)
{
    if (tmo <= 0) {
        return -1;
    }
    auto ret = cv->wait(mutex, std::chrono::nanoseconds(ticks2ns(tmo)));
    return ret == ETIMEDOUT ? -1 : 0;
}
