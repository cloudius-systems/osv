/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef PTHRAD_HH_
#define PTHREAD_HH_

#ifdef __cplusplus
extern "C" {
#endif

// Linux's <time.h> defines 9 types of clocks. We reserve space for 16 slots
// and use the clock ids afterwards for per-thread clocks. This is OSv-
// specific, and an application doesn't need to know about it - only
// pthread_getcpuclockid() and clock_gettime() need to know about this.
#define _OSV_CLOCK_SLOTS 16

#ifdef __cplusplus
}
#endif

#endif /* PTHREAD_HH_ */
