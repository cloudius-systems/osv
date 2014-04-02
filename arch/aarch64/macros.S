/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

.macro push_pair reg1, reg2
        stp \reg1, \reg2, [sp, #-16]!
.endm

.macro pop_pair reg1, reg2
        ldp \reg1, \reg2, [sp], #16
.endm
