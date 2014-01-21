/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _ALTERNATIVE_HH_
#define _ALTERNATIVE_HH_

#define ALTERNATIVE(cond, x, y) do {    \
    if (!cond) {                        \
        do  x  while (0);               \
    } else {                            \
        do  y  while (0);               \
    }                                   \
} while (0)

#endif
