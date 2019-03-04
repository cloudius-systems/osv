# Copyright (C) 2019 Waldemar Kozaczuk
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.

#include "processor-flags.h"

#define BOOT_CR0 ( X86_CR0_PE \
                 | X86_CR0_WP \
                 | X86_CR0_PG )

#define BOOT_CR4 ( X86_CR4_DE         \
                 | X86_CR4_PSE        \
                 | X86_CR4_PAE        \
                 | X86_CR4_PGE        \
                 | X86_CR4_PCE        \
                 | X86_CR4_OSFXSR     \
                 | X86_CR4_OSXMMEXCPT )

.text
.code64
.global vmlinux_entry64
vmlinux_entry64:
    # The address of boot_params structed is passed in RSI
    # register so pass it to extract_linux_boot_params fuction
    # which will extract cmdline and memory information and verify
    # that loader.elf was indeed called as Linux 64-bit vmlinux ELF
    mov %rsi, %rdi
    call extract_linux_boot_params

    # Even though we are in 64-bit long mode we need to reset
    # page tables and other CPU settings the way OSv expects it

    # Enable PAE (Physical Address Extension) - ability to address 64GB
    # TODO: Add more comments to processor-flags.h what each flag does
    mov $BOOT_CR4, %rax
    mov %rax, %cr4

    # Set root of a page table in cr3
    lea ident_pt_l4, %rax
    mov %rax, %cr3

    # Set long mode
    mov $0xc0000080, %ecx // EFER MSR number
    mov $0x00000900, %eax // Set LME = 1
    xor %edx, %edx
    wrmsr // Write contents of EDX:EAX (0:to Model Specific Register specified by ECX register

    # Activate paging and ...?
    # TODO: Add more comments to processor-flags.h what each flag does
    mov $BOOT_CR0, %rax
    mov %rax, %cr0

    mov $OSV_KERNEL_BASE, %rbp
    mov $0x1000, %rbx
    jmp start64
