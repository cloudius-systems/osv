# Copyright (C) 2013 Cloudius Systems, Ltd.
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
.code32

.data
.align 4096
.global ident_pt_l4
ident_pt_l4:
    # The addresses of the paging tables have to be the physical ones, so we have to
    # manually subtract OSV_KERNEL_VM_SHIFT in all relevant places
    .quad ident_pt_l3 + 0x67 - OSV_KERNEL_VM_SHIFT
    .rept 511
    .quad 0
    .endr
#if OSV_KERNEL_VM_SHIFT != 0x40000000 && OSV_KERNEL_VM_SHIFT != 0
#error This code only works correctly for OSV_KERNEL_VM_SHIFT = 0x40000000 or 0
#endif
ident_pt_l3:
    # Each of the 512 entries in this table maps the very 1st 512 GiB of
    # virtual address space 1 GiB at a time
    # The very 1st entry maps 1st GiB 1:1 by pointing to ident_pt_l2 table
    # that specifies addresses of every one of 512 2MiB slots of physical memory
    .quad ident_pt_l2 + 0x67 - OSV_KERNEL_VM_SHIFT
    # The 2nd entry maps 2nd GiB to the same 1st GiB of physical memory by pointing
    # to the same ident_pt_l2 table as the 1st entry above
    # This way we effectively provide correct mapping for the kernel linked
    # to start at 1 GiB + 2 MiB (0x40200000) in virtual memory and point to
    # 2 MiB address (0x200000) where it starts in physical memory
    .quad ident_pt_l2 + 0x67 - OSV_KERNEL_VM_SHIFT
    .rept 510
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
     # subtract OSV_KERNEL_VM_SHIFT because when gdt_desc is referenced, the memory is mapped 1:1
    .long gdt - OSV_KERNEL_VM_SHIFT

# Set up the 64-bit compatible version of GDT description structure
# that points to the same GDT (Global segments Descriptors Table) and
# is used in vmlinux_entry64 to switch back to the protected (32-bit) mode
# from long mode (64-bit).
# Please note the address of the GDT is a 8-bytes-long field instead of
# 4-bytes only in regular 32 version (gdt_desc)
.align 8
gdt64_desc:
    .short gdt_end - gdt - 1
     # subtract OSV_KERNEL_VM_SHIFT because when gdt64_desc is referenced, the memory is mapped 1:1
    .quad gdt - OSV_KERNEL_VM_SHIFT

.align 8
gdt = . - 8
    .quad 0x00af9b000000ffff # 64-bit code segment
    .quad 0x00cf93000000ffff # 64-bit data segment
    .quad 0x00cf9b000000ffff # 32-bit code segment
gdt_end = .
.globl gdt64_desc

.align 8
. = . + 4  # make sure tss_ist is aligned on a quad boundary

.bss

.align 16
. = . + 4096*10
init_stack_top = .

.text

.globl start32
.globl start32_from_vmlinuz
.globl start32_from_64
start32:
    mov $0x0, %edi

start32_from_vmlinuz:
    # Because the memory is mapped 1:1 at this point, we have to manualy
    # subtract OSV_KERNEL_VM_SHIFT from virtual addresses in all relevant places
    lgdt gdt_desc-OSV_KERNEL_VM_SHIFT

# Add an address the vmlinux_entry64 will jump to when
# switching from 64-bit to 32-bit mode
start32_from_64:
    mov $0x10, %eax
    mov %eax, %ds
    mov %eax, %es
    mov %eax, %fs
    mov %eax, %gs
    mov %eax, %ss
    ljmp $0x18, $1f-OSV_KERNEL_VM_SHIFT
1:
    and $~7, %esp
    # Enable PAE (Physical Address Extension) - ability to address 64GB
    # TODO: Add more comments to processor-flags.h what each flag does
    mov $BOOT_CR4, %eax
    mov %eax, %cr4

    # Set root of a page table in cr3
    lea ident_pt_l4, %eax
    # The address of the root paging table has to be physical
    # so substract OSV_KERNEL_VM_SHIFT from ident_pt_l4
    sub $OSV_KERNEL_VM_SHIFT, %eax
    mov %eax, %cr3

    # Set long mode
    mov $0xc0000080, %ecx // EFER MSR number
    mov $0x00000900, %eax // Set LME = 1
    xor %edx, %edx
    wrmsr // Write contents of EDX:EAX (0:to Model Specific Register specified by ECX register

    # Activate paging and ...?
    # TODO: Add more comments to processor-flags.h what each flag does
    mov $BOOT_CR0, %eax
    mov %eax, %cr0

    ljmpl $8, $start64
.code64
.global start64
start64:
    .cfi_startproc simple
    .cfi_def_cfa %rsp, 0
    .cfi_undefined %rip
    # Check for non-zero value in RDI register to detect if
    # it contains the address of the boot_params structure
    # that would be set if we came here from vmlinux_entry64
    cmp $0x0, %rdi
    jz detect_pvh_boot
    call extract_linux_boot_params
    mov $0x1000, %rbx

detect_pvh_boot:
    mov hvm_xen_start_info, %rdi
    cmp $0x0, %rdi
    jz start64_continue
    call hvm_xen_extract_boot_params
    mov $0x1000, %rbx

start64_continue:
    lea .bss, %rdi
    lea .edata, %rcx
    sub %rdi, %rcx
    xor %eax, %eax
    rep stosb
    mov $OSV_KERNEL_BASE, %rbp
    mov %rbp, elf_header
    # %ebx is set by boot16.S before running the loader
    mov %rbx, osv_multiboot_info
    lea init_stack_top, %rsp
    call premain
    mov __loader_argc, %edi
    mov __loader_argv, %rsi
    call main
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
    sub $OSV_KERNEL_VM_SHIFT, %eax
    mov %eax, %cr3
    mov smpboot_efer-smpboot, %eax
    mov smpboot_efer+4-smpboot, %edx
    btr $10, %eax
    mov $0xc0000080, %ecx
    wrmsr
    mov smpboot_cr0-smpboot, %eax
    #1: jmp 1b
    mov %eax, %cr0 # now with paging
    ljmp $8, $smpboot64

smpboot_gdt_desc:
    .short gdt_end - gdt - 1
    .long gdt - OSV_KERNEL_VM_SHIFT
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
