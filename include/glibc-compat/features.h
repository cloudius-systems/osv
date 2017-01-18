/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef GLIBCCOMPAT_FEATURES_H_
#define GLIBCCOMPAT_FEATURES_H_

#include_next <features.h>

#define __GNU_LIBRARY__ 6
#define __GLIBC__       2
#define __GLIBC_MINOR__ 20

#define __GLIBC_PREREQ(maj, min) \
        ((__GLIBC__ << 16) + __GLIBC_MINOR__ >= ((maj) << 16) + (min))

#endif /* FEATURES_H_ */
