/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/eventfd.h>
#include <debug.hh>
#include "libc.hh"

int eventfd(unsigned init, int flags)
{
    debug("eventfd not implemented\n");
    return libc_error(ENOSYS);
}

