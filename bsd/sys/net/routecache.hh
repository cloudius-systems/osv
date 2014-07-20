/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// FreeBSD's routing table, in route.cc and radix.cc, is fairly slow when
// used in the fast path to decide where to send each individual packet:
// The radix tree holding the routes is protected by a rwlock which needs
// to be locked for reading (which is twice slower than a normal lock),
// and and then the route we found is individually locked and unlock.
// Even when uncontended on a single-CPU VM, all this locking and unlocking
// is pretty slow (e.g., in one memcached run I saw this responsible for more
// than 1.5 locks per second), but when we have an SMP with many CPUs
// accessing the network, things get even worse because we start seeing
// contention on these mutexes.
//
// What we really need is to assume that the routing table rarely changes
// an have an RCU routing table, where the read path (the packet-sending
// fast path) involves no locks, and only the write path (changing a route)
// involves a mutex and creation of a new copy of the routing table.
// However, instead of rewriting FreeBSD's route.cc and radix.cc, and the
// countless places that use it and make subtle assumptions on how it works,
// we decided to do this:
//
// 1. In this file, we define a "routing cache", an RCU-based layer in
//    front of the usual routing table.
//
// 2. A new function looks up in the routing cache first, and only if
//    it can't find the route there, it looks up in the regular table,
//    with all the locks as usual - and places the found route on the
//    cache.
//    We should use this function whenever it makes sense and performance
//    is important. We don't have to change all the existing code to use it.
//
// 3. A new function invalidates the routing cache. It should be called
//    whenever routes are modified. Missing a few places is not a disaster
//    but can lead to the wrong route being used in esoteric places. It
//    would be better to find a good place to call this function every
//    time (e.g., perhaps write-unlock of the radix tree is a good place?
//    or not good enough if just a route itself is modified?).

#ifndef INCLUDED_ROUTECACHE_HH
#define INCLUDED_ROUTECACHE_HH

// FIXME: probably most of these includes are unnecessary
#include <bsd/porting/netport.h>
#include <bsd/porting/sync_stub.h>
#include <bsd/sys/sys/param.h>
#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/domain.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/netinet/in_var.h>

#include <bsd/sys/net/route.h>

#include <osv/rcu.hh>
#include <unordered_map>
#include <functional>


class route_cache {
public:
    // Note that this returns a copy of a routing entry, *not* a pointer.
    // So the return value shouldn't be written to, nor, of course, be RTFREE'd.
    static void lookup(struct bsd_sockaddr_in *dst, u_int fibnum, struct rtentry *ret) {
        // A slow implementation
        struct route ro {};
        ro.ro_dst = *(struct bsd_sockaddr *)dst;
        in_rtalloc_ign(&ro, 0, fibnum);
        memcpy(ret, ro.ro_rt, sizeof(*ret));
        RO_RTFREE(&ro);
        ret->rt_refcnt = -1; // try to catch some monkey-business
        mutex_init(&ret->rt_mtx._mutex); // try to catch some monkey-business?
    }

    static void invalidate() {

    }
};

#endif
