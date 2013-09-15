# Copyright (C) 2013 Cloudius Systems, Ltd.
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.

.text
.code32

mb_magic = 0x1BADB002
mb_flags = 0x00000001    # flags: 4k module alignment

multiboot:
    .long mb_magic
    .long mb_flags
    .long 0-mb_magic-mb_flags

.global start32
start32:
	lea elf64_header, %eax
	jmp *24+elf64_header

.align 4096

elf64_header:
.incbin "loader.elf"
