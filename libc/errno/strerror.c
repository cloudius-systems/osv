/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <errno.h>

/* glibc's English messages (strerror's, libc/locale/messages-en.inc), with
 * glibc's sys_nerr: one past the last code it names. */
#define nullptr ((const char *)0)
#define EN_ERRORS_ONLY
#define en_errors sys_errlist
#include "../locale/messages-en.inc"

const int sys_nerr = sizeof(sys_errlist) / sizeof(sys_errlist[0]);
