/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

cmdline_addr = . + 0x200 // 512 bytes after start of image
target = . + 0x10000

.text
.align 16

.globl prestart
prestart: // x4 = 0x40080000 set by qemu bootloader
        adr     x4, cmdline_addr
        adr     x3, target /* elf header start */

        // load entry point from ELF header + 24
        ldr     w0, [x3, #24]
        ldr     w1, [x3, #28]

        orr     x0, x0, x1, LSL #32
        br      x0
