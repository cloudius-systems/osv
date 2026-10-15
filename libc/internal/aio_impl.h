/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * musl 1.2.x split these declarations out into src/internal/aio_impl.h,
 * which is not on OSv's musl include path.  OSv does not build musl's
 * aio, so the weak dummy in __stdio_close.c is the only definition of
 * __aio_close and this only needs to declare it.  Mirrors the other
 * libc/internal shims (lock.h, locale_impl.h, pthread_stubs.h).
 *
 * `hidden` comes from musl/src/include/features.h, already on the
 * include path for every musl object.
 */

#ifndef AIO_IMPL_H
#define AIO_IMPL_H

extern hidden volatile int __aio_fut;

extern hidden int __aio_close(int);
extern hidden void __aio_atfork(int);

#endif
