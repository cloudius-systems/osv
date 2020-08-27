/*
 * Copyright (C) 2017 ScyllaDB, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/types.h>
#include <osv/stubbing.hh>
#include <fenv.h>
#include <__fenv.h>
// Note that musl's fenv.h does not define feenableexcept and friends, so
// we need to 'extern "C"' them here, as no header file does this.

extern "C"
int feenableexcept(int mask)
{
    WARN_STUBBED();
    // The feenableexcept says it returns -1 on failure.
    return -1;
}

extern "C"
int fedisableexcept(int mask)
{
    WARN_STUBBED();
    return -1;
}

extern "C"
int fegetexcept()
{
    WARN_STUBBED();
    return -1;
}
