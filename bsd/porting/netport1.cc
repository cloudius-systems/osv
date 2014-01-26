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
    auto ret = condvar_wait(cv, mutex, clock::get()->time() + ticks2ns(tmo));
    return ret == ETIMEDOUT ? -1 : 0;
}

extern "C"
int getmntent(FILE* fp, struct mnttab* me)
{
    // FIXME: ignore fp but return the mounts directly from vfs instead
    // should be replaced by a virtual file
    // FIXME: we're using a static here in lieu of the file offset
    static size_t last = 0;
    auto mounts = osv::current_mounts();
    if (last >= mounts.size()) {
        last = 0;
        return -1;
    } else {
        // We're using statics here, but there's nothing else we can do with
        // this horrible interface
        static char path[PATH_MAX];
        static char special[PATH_MAX];
        static char options[PATH_MAX];
        static char type[30];
        auto& m = mounts[last++];
        strcpy(me->mnt_mountp = path, m.path.c_str());
        strcpy(me->mnt_fstype = type, m.type.c_str());
        strcpy(me->mnt_mntopts = options, m.options.c_str());
        strcpy(me->mnt_special = special, m.special.c_str());
        return 0;
    }
}
