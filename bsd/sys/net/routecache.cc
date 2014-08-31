/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "routecache.hh"
#include "osv/prio.hh"

osv::rcu_ptr<route_cache::routemap, osv::rcu_deleter<route_cache::routemap>> route_cache::cache
    __attribute__((init_priority((int)init_prio::routecache)))
    (new route_cache::routemap);

mutex route_cache::cache_mutex
    __attribute__((init_priority((int)init_prio::routecache)));
