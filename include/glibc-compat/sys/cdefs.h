/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef CDEFS_H_
#define CDEFS_H_

#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#define __THROW throw ()
#else
#define __BEGIN_DECLS
#define __END_DECLS
#define __THROW
#endif

#define __always_inline __attribute__((always_inline))
#define __flexarr []

#endif /* CDEFS_H_ */
