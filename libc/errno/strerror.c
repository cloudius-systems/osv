/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <errno.h>

const char *const sys_errlist[] = {
#define E(num, message) [num] = message,
#include "../../musl/src/errno/__strerror.h"
#undef E
};

const int sys_nerr = sizeof(sys_errlist) / sizeof(sys_errlist[0]);
