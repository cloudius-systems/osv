/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_BITS_FLOAT_H
#define OSV_BITS_FLOAT_H

#define FLT_ROUNDS 1
#define FLT_EVAL_METHOD 0

#define LDBL_MIN     3.36210314311209350626267781732175260e-4932L
#define LDBL_MAX     1.18973149535723176508575932662800702e+4932L
#define LDBL_EPSILON 1.92592994438723585305597794258492732e-34L

#define LDBL_MANT_DIG 113
#define LDBL_MIN_EXP (-16381)
#define LDBL_MAX_EXP 16384

/* XXX HACK XXX */
/* libc/ math stuff does not handle LDBL_MANT_DIG 113 well:
   the modules recognize the case and handle it with the same code
   as LDBL_MANT_DIG=64, but the referenced implementation in
   __invtrigl.c et al do not provide an implementation for
   113, so we are left with unresolved symbols.

   So we redefine to 64, which is probably wrong.
 */

#undef LDBL_MANT_DIG
#define LDBL_MANT_DIG 64

#define LDBL_DIG 33
#define LDBL_MIN_10_EXP (-4931)
#define LDBL_MAX_10_EXP 4932

#define DECIMAL_DIG 36

#endif /* OSV_BITS_FLOAT_H */
