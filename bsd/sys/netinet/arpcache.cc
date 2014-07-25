/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <bsd/sys/netinet/arpcache.hh>
#include <bsd/sys/net/if_llatbl.h>

struct arp_cache global_arp_cache{};

void arp_cache_add(const struct llentry *lle)
{
    if (!(lle->la_flags & LLE_VALID)) {
        return;
    }
    auto* mac = reinterpret_cast<const arp_cache::mac_address*>(lle->ll_addr.mac16);
    auto* sin = satosin(L3_ADDR(lle));
    global_arp_cache.add(sin->sin_addr, *mac, lle->la_flags);
}

void arp_cache_remove(const struct llentry *lle)
{
    auto* sin = satosin(L3_ADDR(lle));
    global_arp_cache.remove(sin->sin_addr);
}

bool arp_cache_lookup(const in_addr ip, arp_cache::mac_address& mac, u16& flags)
{
    auto e = global_arp_cache.lookup(ip);
    if (e) {
        mac = e->mac;
        flags = e->flags;
        return true;
    }
    return false;
}
