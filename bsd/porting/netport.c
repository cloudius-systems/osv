/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <errno.h>
#include <memory.h>

#include <sys/time.h>
#include <bsd/porting/netport.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/net/if_var.h>
#include <bsd/sys/net/if_media.h>

int copyin(const void *uaddr, void *kaddr, size_t len)
{
    memcpy(kaddr, uaddr, len);
    return (0);
}

int copyout(const void *kaddr, void *uaddr, size_t len)
{
    memcpy(uaddr, kaddr, len);
    return (0);
}

int copystr(const void *kfaddr, void *kdaddr, size_t len, size_t *done)
{
    // FIXME: implement
    return (0);
}

int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done)
{
    // FIXME: implement
    return (0);
}

int ppsratecheck(struct timeval *lasttime, int *curpps, int maxpps)
{
    struct timeval now;
    getmicrotime(&now);
    uint64_t now2 = now.tv_sec * hz + now.tv_usec;

    /*
     * Reset the last time and counter if this is the first call
     * or more than a second has passed since the last update of
     * lasttime.
     */
    if (lasttime->tv_sec == 0 || (u_int)(now2 - lasttime->tv_sec) >= hz) {
        lasttime->tv_sec = now2;
        *curpps = 1;
        return (maxpps != 0);
    } else {
        (*curpps)++;        /* NB: ignore potential overflow */
        return (maxpps < 0 || *curpps < maxpps);
    }
}

static void
timevalfix(struct timeval *t1)
{

    if (t1->tv_usec < 0) {
        t1->tv_sec--;
        t1->tv_usec += 1000000000;
    }
    if (t1->tv_usec >= 1000000000) {
        t1->tv_sec++;
        t1->tv_usec -= 1000000000;
    }
}

static void
timevalsub(struct timeval *t1, const struct timeval *t2)
{

    t1->tv_sec -= t2->tv_sec;
    t1->tv_usec -= t2->tv_usec;
    timevalfix(t1);
}

#define timevalcmp(tvp, uvp, cmp)                   \
    (((tvp)->tv_sec == (uvp)->tv_sec) ?             \
        ((tvp)->tv_usec cmp (uvp)->tv_usec) :           \
        ((tvp)->tv_sec cmp (uvp)->tv_sec))

int ratecheck(struct timeval *lasttime, const struct timeval *mininterval)
{
    struct timeval tv, delta;
    int rv = 0;

    getmicrotime(&tv);
    delta = tv;
    timevalsub(&delta, lasttime);

    /*
     * check for 0,0 is so that the message will be seen at least once,
     * even if interval is huge.
     */
    if (timevalcmp(&delta, mininterval, >=) ||
        (lasttime->tv_sec == 0 && lasttime->tv_usec == 0)) {
        *lasttime = tv;
        rv = 1;
    }

    return (rv);
}

int tvtohz(struct timeval *tv)
{
    return (tv->tv_sec*hz + tv->tv_usec);
}

void
ifmedia_init(struct ifmedia *ifm, int dontcare_mask, ifm_change_cb_t change_callback,
             ifm_stat_cb_t status_callback)
{
}

void
ifmedia_add(struct ifmedia *ifm, int mword, int data, void *aux)
{
}
void
ifmedia_set(struct ifmedia *ifm, int target)
{
}

int
ifmedia_ioctl(struct ifnet *ifp, struct bsd_ifreq *ifr, struct ifmedia *ifm, u_long cmd)
{
    return -ENOSYS;
}
