/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef LIBC_HH_
#define LIBC_HH_

#include <errno.h>
#include "internal/libc.h" // for the macros

int libc_error(int err);

template <typename T>
T* libc_error_ptr(int err);

template <typename T>
T* libc_error_ptr(int err)
{
    libc_error(err);
    return nullptr;
}

#endif /* LIBC_HH_ */
