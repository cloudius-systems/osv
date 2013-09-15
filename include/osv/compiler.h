/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef COMPILER_H_
#define COMPILER_H_

#ifdef HAVE_ATTR_COLD_LABEL
#  define ATTR_COLD_LABEL __attribute__((cold))
#else
#  define ATTR_COLD_LABEL
#endif

#endif /* COMPILER_H_ */
