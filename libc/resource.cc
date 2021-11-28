/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>

#include <osv/debug.hh>
#include <osv/sched.hh>
#include <osv/clock.hh>

#include "libc.hh"

using namespace std::chrono;

int getrusage(int who, struct rusage *usage)
{
    memset(usage, 0, sizeof(*usage));
    switch (who) {
    case RUSAGE_THREAD:
        fill_tv(sched::osv_run_stats(), &usage->ru_utime);
        break;
    case RUSAGE_SELF:
        fill_tv(sched::process_cputime(), &usage->ru_utime);
        break;
    default:
        errno = EINVAL;
        return -1;

    }
    return 0;
}
