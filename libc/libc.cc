/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "libc.hh"
#include <osv/sched.hh>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <boost/algorithm/string/split.hpp>
#include <type_traits>
#include <limits>
#include <sys/resource.h>
#include <pwd.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <osv/debug.h>
#include <sched.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <osv/clock.hh>
#include <osv/mempool.hh>
#include <osv/version.h>

// FIXME: If we ever support multiple different executables we will have to maybe put those
// on a shared library
char *program_invocation_name;
char *program_invocation_short_name;

int libc_error(int err)
{
    errno = err;
    return -1;
}

#undef errno

int __thread errno;

int* __errno_location()
{
    return &errno;
}

int getrlimit(int resource, struct rlimit *rlim)
{
    auto set = [=] (rlim_t r) { rlim->rlim_cur = rlim->rlim_max = r; };
    switch (resource) {
    case RLIMIT_STACK: {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        size_t stacksize;
        pthread_attr_getstacksize(&attr, &stacksize);
        set(stacksize);
        pthread_attr_destroy(&attr);
        break;
    }
    case RLIMIT_NOFILE:
        set(1024*10); // FIXME: larger?
        break;
    case RLIMIT_CORE:
        set(RLIM_INFINITY);
        break;
    case RLIMIT_NPROC:
        set(RLIM_INFINITY);
        break;
    case RLIMIT_AS:
    case RLIMIT_DATA:
    case RLIMIT_RSS:
        set(RLIM_INFINITY);
        break;
    default:
        kprintf("getrlimit: resource %d not supported\n", resource);
        abort();
    }
    return 0;
}

int setrlimit(int resource, const struct rlimit *rlim)
{
    // osv - no limits
    return 0;
}
LFS64(getrlimit);
LFS64(setrlimit);

uid_t geteuid()
{
    return 0;
}

int sched_yield()
{
    sched::thread::yield();
    return 0;
}

extern "C"
int getloadavg(double loadavg[], int nelem)
{
    int i;

    for (i = 0; i < nelem; i++)
        loadavg[i] = 0.5;

    return 0;
}

extern "C" int sysinfo(struct sysinfo *info)
{
    info->uptime = std::chrono::duration_cast<std::chrono::seconds>(
                osv::clock::uptime::now().time_since_epoch()).count();
    info->loads[0] = info->loads[1] = info->loads[2] = 0; // TODO
    info->totalram = memory::stats::total();
    info->freeram = memory::stats::free();
    info->sharedram = 0; // TODO: anything more meaningful to return?
    info->bufferram = 0; // TODO: anything more meaningful to return?
    info->totalswap = 0; // Swap not supported in OSv
    info->freeswap = 0; // Swap not supported in OSv
    info->procs = 1; // Only one process in OSv
    info->totalhigh = info->freehigh = 0; // No such concept in OSv
    info->mem_unit = 1;
    return 0;
}

int tcgetattr(int fd, termios *p)
{
    return ioctl(fd, TCGETS, p);
}

int tcsetattr(int fd, int action, const termios *p)
{
    switch (action) {
    case TCSANOW:
        break;
    case TCSADRAIN:
        tcdrain(fd);
        break;
    case TCSAFLUSH:
        tcdrain(fd);
        tcflush(fd,TCIFLUSH);
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    return ioctl(fd, TCSETS, p);
}

int tcdrain(int fd)
{
    // The archaic TCSBRK is customary on Linux for draining output.
    // BSD would have used TIOCDRAIN.
    return ioctl(fd, TCSBRK, 1);
}

int tcflush(int fd, int what)
{
    // Linux uses TCFLSH. BSD would have used TIOCFLUSH (and different
    // argument).
    return ioctl(fd, TCFLSH, what);
}

speed_t cfgetospeed(const termios *p)
{
    return p->__c_ospeed;
}

extern "C" {
    const char *gnu_get_libc_version(void)
    {
        return OSV_VERSION;
    }

    const char *gnu_get_libc_release(void)
    {
        return "OSv";
    }
}
