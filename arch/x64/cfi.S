# Copyright (C) 2013 Cloudius Systems, Ltd.
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.

# macros for cfi annotations

.macro pushq_cfi reg
	pushq \reg
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset \reg, 0
.endm

.macro popq_cfi reg
	popq \reg
	.cfi_adjust_cfa_offset -8
	.cfi_restore \reg
.endm

