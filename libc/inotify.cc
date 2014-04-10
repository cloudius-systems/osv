/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <api/sys/inotify.h>
#include <osv/stubbing.hh>
#include "libc.hh"

int inotify_init()
{
    WARN_STUBBED();

    return libc_error(EMFILE);
}

int inotify_init1(int flags)
{
    WARN_STUBBED();

    return libc_error(EMFILE);
}

int inotify_add_watch(int fd, const char *pathname, uint32_t mask)
{
    WARN_STUBBED();

    return libc_error(EINVAL);
}

int inotify_rm_watch(int fd, int wd)
{
    WARN_STUBBED();

    return libc_error(EINVAL);
}
