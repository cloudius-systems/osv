/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef SHRINKER_H_
#define SHRINKER_H_

void __attribute__((constructor)) bsd_shrinker_init(void);

#endif
