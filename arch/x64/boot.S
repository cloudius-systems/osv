# Copyright (C) 2013 Cloudius Systems, Ltd.
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.

#include "processor-flags.h"

.text
.code32

.data
.align 4096
ident_pt_l4:
    .quad ident_pt_l3 + 0x67
    .rept 511
    .quad 0
    .endr
ident_pt_l3:
    .quad ident_pt_l2 + 0x67
    .rept 511
    .quad 0
    .endr
ident_pt_l2:
    index = 0
    .rept 512
    .quad (index << 21) + 0x1e7
    index = index + 1
    .endr

gdt_desc:
    .short gdt_end - gdt - 1
    .long gdt

.align 8
gdt = . - 8
    .quad 0x00af9b000000ffff # 64-bit code segment
    .quad 0x00cf93000000ffff # 64-bit data segment
    .quad 0x00cf9b000000ffff # 32-bit code segment
gdt_end = .

.align 8
. = . + 4  # make sure tss_ist is aligned on a quad boundary

.bss

.align 16
. = . + 4096*4
init_stack_top = .

. = . + 4096*10
.global interrupt_stack_top
interrupt_stack_top = .

.text

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

.globl start32
start32:
    mov %eax, %ebp
    lgdt gdt_desc
    mov $0x10, %eax
    mov %eax, %ds
    mov %eax, %es
    mov %eax, %fs
    mov %eax, %gs
    mov %eax, %ss
    ljmp $0x18, $1f
1:
    and $~7, %esp
    mov $BOOT_CR4, %eax
    mov %eax, %cr4
    lea ident_pt_l4, %eax
    mov %eax, %cr3
    mov $0xc0000080, %ecx
    mov $0x00000900, %eax
    xor %edx, %edx
    wrmsr
    mov $BOOT_CR0, %eax
    mov %eax, %cr0
    ljmpl $8, $start64
.code64
.global start64
start64:
    .cfi_startproc simple
    .cfi_def_cfa %rsp, 0
    .cfi_undefined %rip
    lea .bss, %rdi
    lea .edata, %rcx
    sub %rdi, %rcx
    xor %eax, %eax
    rep stosb
    mov %rbp, elf_header
    mov %rbx, osv_multiboot_info
    lea init_stack_top, %rsp
    call premain
    mov __argc, %edi
    mov __argv, %rsi
    jmp main
    .cfi_endproc

# The smp trampoline must be in the lower 1MB, so we manually relocate
# it to address 0 by subtracting smpboot from any offset
.data
.global smpboot
smpboot:
.code16
	lgdtl smpboot_gdt_desc-smpboot
	mov smpboot_cr0-smpboot, %eax
	btr $31, %eax # disable paging
	mov %eax, %cr0
	ljmp $0x18, $1f-smpboot
1:
.code32
	mov $0x10, %eax
	mov %eax, %ds
	mov %eax, %es
	mov %eax, %ss
	mov %eax, %fs
	mov %eax, %gs
	mov smpboot_cr4-smpboot, %eax
	mov %eax, %cr4
	lea ident_pt_l4, %eax
	mov %eax, %cr3
	mov smpboot_efer-smpboot, %eax
	mov smpboot_efer+4-smpboot, %edx
	mov $0xc0000080, %ecx
	wrmsr
	mov smpboot_cr0-smpboot, %eax
	#1: jmp 1b
	mov %eax, %cr0 # now with paging
	ljmp $8, $smpboot64

smpboot_gdt_desc:
	.short gdt_end - gdt - 1
	.long gdt
.global smpboot_cr0
smpboot_cr0:
	.long 0
.global smpboot_cr4
smpboot_cr4:
	.long 0
.global smpboot_efer
smpboot_efer:
	.quad 0

.global smpboot_end
smpboot_end = .

.bss

.global smpboot_cr3
smpboot_cr3: .quad 0

.global smp_stack_free
smp_stack_free: .quad 0

# 'ltr' expects an available TSS, then marks it busy, so only one
# can be loaded at a time
trlock: .byte 0

.text
.code64

smpboot64:
	mov smpboot_cr3, %rax
	mov %rax, %cr3
1:
	mov smp_stack_free, %rax
	mov 4096(%rax), %rbx
	lock cmpxchg %rbx, smp_stack_free
	jnz 1b
	lea 4096(%rax), %rsp
	call smp_main
