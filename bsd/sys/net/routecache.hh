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

#include <bsd/porting/netport.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/in_var.h>
#include <bsd/sys/net/route.h>

#include <osv/rcu.hh>
#include <unordered_map>
#include <functional>

#include <osv/kernel_config_lazy_stack.h>
#include <osv/kernel_config_lazy_stack_invariant.h>

// rtentry contains a mutex which cannot be copied. nonlockable_rtentry
// is layout-compatible with rtentry, but does not support locking so
// it does support the copy constructor.
class nonlockable_rtentry : public rtentry {
public:
    nonlockable_rtentry(const nonlockable_rtentry &other) {
        memcpy((void*) this, (const void*) &other, sizeof(*this));
        // try to catch some monkey business
        rt_refcnt = -1;
#if 0
        mutex_init(&rt_mtx._mutex);
#endif
    }
    nonlockable_rtentry() {
        // This shouldn't be necessary, but cache[dst] = ... uses it
        // because it first allocates an empty entry and then copies :(
    }
    nonlockable_rtentry &operator=(const nonlockable_rtentry &other) {
        memcpy((void*) this, (const void*) &other, sizeof(*this));
        // try to catch some monkey business
        rt_refcnt = -1;
#if 0
        mutex_init(&rt_mtx._mutex);
#endif
        return *this;
    }

    bool is_loopback(void) const {
        return (rt_ifp && (rt_ifp->if_flags & IFF_LOOPBACK)) ? true : false;
    }
};

// Silly routing table implementation, allowing search given address in list
// list address ranges (address+netmask). This silly implementation is IPv4
// only (assumes addresses are u32) and performs a linear search, which only
// makes sense when there are only a few entries (on a typical non-router
// system, there will be only one).
// TODO: Use the radix tree implementation that BSD already has (radix.cc,
// route.cc), but it is incredibly complex.
class silly_rtable {
    // Limit the number of routing entries cached to some reasonable small
    // limit. This is an O(N) data structure, after all...
    static constexpr unsigned max_entries = 10;
    struct silly_rtable_entry {
        u32 address;
        u32 netmask;
        nonlockable_rtentry rte;
        silly_rtable_entry(u32 a, u32 n, const nonlockable_rtentry &r) :
            address(a), netmask(n), rte(r) { }
    };
    std::list<silly_rtable_entry> entries;
public:
    void add(u32 a, u32 n, const nonlockable_rtentry &r) {
        while (entries.size() >= max_entries) {
            entries.pop_back();
        }
        entries.emplace_front(a, n, r);
    }
    // address should be in host order
    bool is_loopback_net(u32 address) const {
        return ((address >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET) ? true : false;
    }
    nonlockable_rtentry *search(u32 address) {
        for (silly_rtable_entry &e : entries) {
            if (e.rte.is_loopback() == false) {
                // The interface associated with this entry is non-loopback
                // so it is enough to compare the networks of the addresses
                if ((e.address & e.netmask) == (address & e.netmask)) {
                    return &e.rte;
                }
            } else {
                // Given the interface associated with this entry is a loopback one,
                // consider addresses like 127.0.0.1 or 127.0.0.2 as a match
                if (is_loopback_net(address)) {
                    return &e.rte;
                }
                // Otherwise, we shouldn't use this entry on IP addresses just because they're
                // on the same network as our non-loopback address. So match the entire
                // address.
                // This would handle a case when this route entry was added
                // when looking up a non-loopback address like 192.168.122.15 but
                // "converted" or handled as loopback as part of an optimization.
                // The good practical example is an app like golang-httpclient
                // which calls httpserver-api listening on 192.168.122.15
                if (e.address == address) {
                    return &e.rte;
                }
            }
        }
        return nullptr;
    }
};


class route_cache {
    using routemap = silly_rtable;
    static osv::rcu_ptr<routemap, osv::rcu_deleter<routemap>> cache;
    static mutex cache_mutex;
public:
    // Note that this returns a copy of a routing entry, *not* a pointer.
    // So the return value shouldn't be written to, nor, of course, be RTFREE'd.
    //
    // Returns true when lookup succeeded, false otherwise
    static bool lookup(struct bsd_sockaddr_in *dst, u_int fibnum, struct rtentry *ret) {
        // Only support fib 0, which is what we use anyway (see rt_numfibs in
        // route.cc).
        assert(fibnum == 0);

#if CONF_lazy_stack_invariant
        assert(sched::preemptable() && arch::irq_enabled());
#endif
#if CONF_lazy_stack
        arch::ensure_next_stack_page();
#endif
        WITH_LOCK(osv::rcu_read_lock) {
            auto *c = cache.read();
            auto entry = c->search(dst->sin_addr.s_addr);
            if (entry) {
                memcpy(ret, entry, sizeof(*ret));
                return true;
            }
        }
        // Not found in cache. Do the slow lookup
        struct route ro {};
        ro.ro_dst = *(struct bsd_sockaddr *)dst;
        in_rtalloc_ign(&ro, 0, fibnum);
        if (!ro.ro_rt) {
            RO_RTFREE(&ro);
            return false;
        }
        memcpy(ret, ro.ro_rt, sizeof(*ret));
        RO_RTFREE(&ro);
        ret->rt_refcnt = -1; // try to catch some monkey-business
#if 0
        mutex_init(&ret->rt_mtx._mutex); // try to catch some monkey-business?
#endif
        // Add the result to the cache
        WITH_LOCK(cache_mutex) {
            auto *old_cache = cache.read_by_owner();
            auto new_cache = new routemap(*old_cache);
            auto netmask = ((bsd_sockaddr_in *)(ret->rt_ifa->ifa_netmask))->sin_addr.s_addr;
            auto addr = dst->sin_addr.s_addr;
            new_cache->add(addr, netmask, *(nonlockable_rtentry *)ret);
            cache.assign(new_cache);
            osv::rcu_dispose(old_cache);
        }
        return true;
    }

    static void invalidate() {
        WITH_LOCK(cache_mutex) {
            auto *old_cache = cache.read_by_owner();
            auto new_cache = new routemap();
            cache.assign(new_cache);
            osv::rcu_dispose(old_cache);
        }
    }
};

#endif
