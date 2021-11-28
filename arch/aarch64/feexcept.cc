/*
 * Copyright (C) 2017 ScyllaDB, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*-
 * Copyright (c) 2004-2005 David Schultz <das@FreeBSD.ORG>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <fenv.h>
#include <__fenv.h>
#include <osv/export.h>
// Note that musl's fenv.h does not define feenableexcept and friends, so
// we need to 'extern "C"' them here, as no header file does this.

// Please note that most of the code below comes from newlib
// (file newlib/libc/machine/aarch64/sys/fenv.h as of commit fd5e27d362e9e8582dd4c85e52c50742c518345d)
// almost as-is except where it has been formatted to match this file.

typedef	__uint64_t	_fenv_t;

/* We need to be able to map status flag positions to mask flag positions */
#define _FPUSW_SHIFT	8
#define	_ENABLE_MASK	(FE_ALL_EXCEPT << _FPUSW_SHIFT)

#define	__mrs_fpcr(__r)	__asm __volatile("mrs %0, fpcr" : "=r" (__r))
#define	__msr_fpcr(__r)	__asm __volatile("msr fpcr, %0" : : "r" (__r))

#define	__mrs_fpsr(__r)	__asm __volatile("mrs %0, fpsr" : "=r" (__r))
#define	__msr_fpsr(__r)	__asm __volatile("msr fpsr, %0" : : "r" (__r))

extern "C" OSV_LIBM_API
int feenableexcept(int mask)
{
    _fenv_t __old_r, __new_r;

    __mrs_fpcr(__old_r);
    __new_r = __old_r | ((mask & FE_ALL_EXCEPT) << _FPUSW_SHIFT);
    __msr_fpcr(__new_r);
    return ((__old_r >> _FPUSW_SHIFT) & FE_ALL_EXCEPT);
}

extern "C" OSV_LIBM_API
int fedisableexcept(int mask)
{
    _fenv_t __old_r, __new_r;

    __mrs_fpcr(__old_r);
    __new_r = __old_r & ~((mask & FE_ALL_EXCEPT) << _FPUSW_SHIFT);
    __msr_fpcr(__new_r);
    return ((__old_r >> _FPUSW_SHIFT) & FE_ALL_EXCEPT);
}

extern "C" OSV_LIBM_API
int fegetexcept()
{
    _fenv_t __r;

    __mrs_fpcr(__r);
    return ((__r & _ENABLE_MASK) >> _FPUSW_SHIFT);
}
