/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef INCLUDED_EXECINFO_H
#define INCLUDED_EXECINFO_H

#ifdef __cplusplus
extern "C" {
#endif

int backtrace (void **buffer, int size);

#ifdef __cplusplus
}
#endif

#endif
