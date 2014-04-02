/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_BITS_FENV_H
#define OSV_BITS_FENV_H

/* reference here is the fpsr status */
#define FE_INVALID    0x01 /* IOC */
#define FE_DIVBYZERO  0x02 /* DZC */
#define FE_OVERFLOW   0x04 /* OFC */
#define FE_UNDERFLOW  0x08 /* UFC */
#define FE_INEXACT    0x10 /* IXC */
/* bits 6:5 are RES0 */
#define __FE_DENORM   0x80 /* IDC */

#define FE_ALL_EXCEPT 0x9f

/* routing modes in fpcr rmode */
#define FE_TONEAREST  0x000000
#define FE_UPWARD     0x400000
#define FE_DOWNWARD   0x800000
#define FE_TOWARDZERO 0xc00000

/* floating-point environment */
typedef struct
{
    unsigned int __fpcr;
    unsigned int __fpsr;
} fenv_t;


#define FE_DFL_ENV    ((const fenv_t *) -1L)
/* no idea if we need this NOMASK thing */
#define FE_NOMASK_ENV ((const fenv_t *) -2L)

typedef unsigned int fexcept_t;

#endif /* OSV_BITS_FENV_H */
