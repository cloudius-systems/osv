/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#ifndef INCLUDED_ARPCACHE_HH
#define INCLUDED_ARPCACHE_HH

#include <bsd/porting/netport.h>
#include <bsd/sys/sys/queue.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/ip.h>

#include <osv/rcu-hashtable.hh>
#include <osv/rcu.hh>
#include <osv/types.h>
#include <osv/mutex.h>
#include <osv/sched.hh>

#include <boost/optional.hpp>
#include <functional>

#include <osv/kernel_config_lazy_stack.h>
#include <osv/kernel_config_lazy_stack_invariant.h>

struct llentry;

namespace std {
template <>
struct hash<in_addr> {
    size_t operator()(in_addr x) const { return std::hash<unsigned int>()(x.s_addr); }
};
}

struct arp_cache {
    struct mac_address {
        u8 addr[6];

        bool operator==(const mac_address& m) const
        {
            for (int i = 0; i < 6; i++) {
                if (m.addr[i] != addr[i])
                    return false;
            }
            return true;
        }
    };

    struct entry {
        in_addr ip;
        mac_address mac;
        u16 flags;
        entry(in_addr i, mac_address m, u16 f) : ip(i), mac(m), flags(f) {}
    };

    struct entry_hash : private std::hash<in_addr> {
        size_t operator()(const entry& e) const { return std::hash<in_addr>::operator()(e.ip); }
    };

    struct entry_compare {
        bool operator()(const in_addr& ip, const entry& e) const {
            return ip.s_addr == e.ip.s_addr;
        }
    };

    void add(const in_addr ip, const mac_address mac, const u16 flags)
    {
        WITH_LOCK(_mtx) {
            auto i = _entries.owner_find(ip, std::hash<in_addr>(), entry_compare());
            if (i) {
                if (i->mac == mac && i->flags == flags) {
                    return;
                }
                _entries.erase(i);
            }
            _entries.emplace(ip, mac, flags);
        }
    }

    void remove(const in_addr ip)
    {
        WITH_LOCK(_mtx) {
            auto i = _entries.owner_find(ip, std::hash<in_addr>(), entry_compare());
            if (i)
                _entries.erase(i);
        }
    }

    boost::optional<entry> lookup(const in_addr ip)
    {
#if CONF_lazy_stack_invariant
        assert(sched::preemptable() && arch::irq_enabled());
#endif
#if CONF_lazy_stack
        arch::ensure_next_stack_page();
#endif
        WITH_LOCK(osv::rcu_read_lock) {
            auto i = _entries.reader_find(ip, std::hash<in_addr>(), entry_compare());
            return boost::optional<entry>(!!i, *i);
        }
    }

private:
    mutex _mtx;
    osv::rcu_hashtable<entry, entry_hash> _entries;
};

void arp_cache_add(const struct llentry *lle);
void arp_cache_remove(const struct llentry *lle);
bool arp_cache_lookup(const in_addr ip, arp_cache::mac_address& mac, u16& flags);

#endif
