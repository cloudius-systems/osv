/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef __MIGRATION_LOCK_H__
#define __MIGRATION_LOCK_H__

#include <osv/sched.hh>

class migration_lock_t {
public:
    void lock() { sched::migrate_disable(); }
    void unlock() { sched::migrate_enable(); };
};

namespace {

migration_lock_t migration_lock;

}

#endif
