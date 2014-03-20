/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

target = . + 0x10000
elf_entry = 24 + target

.text
.align 16

.globl prestart
prestart: // x4 = 0x40080000 set by qemu bootloader
    adr x0, target /* elf header start */
    adr x1, elf_entry
    ldr x2, [x1] // load entry point from ELF header
    br  x2
