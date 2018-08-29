/* Copyright 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

.global __sigsetjmp
.global _sigsetjmp
.global sigsetjmp
.type __sigsetjmp,@function
.type _sigsetjmp,@function
.type sigsetjmp,@function
__sigsetjmp:
_sigsetjmp:
sigsetjmp:
        wfi
        b sigsetjmp
