# Copyright (C) 2013 Cloudius Systems, Ltd.
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.

.code16

tmp = 0x80000
bootsect = 0x7c00
cmdline = 0x7e00
target = 0x200000
entry = 24+target

mb_info = 0x1000
mb_cmdline = (mb_info + 16)
mb_mmap_len = (mb_info + 44)
mb_mmap_addr = (mb_info + 48)

e820data = 0x2000

.text

start:
	ljmp $0, $init

# Be conservative, we should not be in the 10th byte so far, but better safe than sorry
.org 0x10
count32: .short 4096 # in 32k units, 4096=128MB

int1342_struct:
	.byte 0x10
	.byte 0
	.short 0x40   # 32k
	.short 0
	.short tmp / 16
lba:
	.quad 128

int1342_boot_struct: # for command line
	.byte 0x10
	.byte 0
	.short 0x3f   # 31.5k
	.short cmdline
	.short 0
	.quad 1

xfer: .long target

gdt:
	.short gdt_size - 1
	.short gdt
	.long 0
	.quad 0x00cf9b000000ffff # 32-bit code segment
	.quad 0x00cf93000000ffff # 32-bit data segment
	.quad 0x00009b000000ffff # 16-bit code segment
	.quad 0x000093000000ffff # 16-bit data segment
gdt_size = . - gdt

init:
	xor %ax, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %ss
	mov $0x7c00, %sp
	lea int1342_boot_struct, %si
	mov $0x42, %ah
	mov $0x80, %dl
	int $0x13
	movl $cmdline, mb_cmdline
read_disk:
	lea int1342_struct, %si
	mov $0x42, %ah
	mov $0x80, %dl
	int $0x13
	jc done_disk # in case of errors, we don't really know what to do.

	cli
	lgdtw gdt
	mov $0x11, %ax
	lmsw %ax
	ljmp $8, $1f
1:
	.code32
	mov $0x10, %ax
	mov %eax, %ds
	mov %eax, %es
	mov $tmp, %esi
	mov xfer, %edi
	mov $0x8000, %ecx
	rep movsb
	mov %edi, xfer
	mov $0x20, %al
	mov %eax, %ds
	mov %eax, %es
	ljmpw $0x18, $1f
1:
	.code16
	mov $0x10, %eax
	mov %eax, %cr0
	ljmpw $0, $1f
1:
	xor %ax, %ax
	mov %ax, %ds
	mov %ax, %es
	sti
	addl $(0x8000 / 0x200), lba
	decw count32
	jnz read_disk
done_disk:

	mov $e820data, %edi
	mov %edi, mb_mmap_addr
	xor %ebx, %ebx
more_e820:
	mov $100, %ecx
	mov $0x534d4150, %edx
	mov $0xe820, %ax
	add $4, %edi
	int $0x15
	jc done_e820
	mov %ecx, -4(%edi)
	add %ecx, %edi
	test %ebx, %ebx
	jnz more_e820
done_e820:
	sub $e820data, %edi
	mov %edi, mb_mmap_len

	cli
	mov $0x11, %ax
	lmsw %ax
	ljmp $8, $1f
1:
	.code32
	mov $0x10, %ax
	mov %eax, %ds
	mov %eax, %es
	mov %eax, %gs
	mov %eax, %fs
	mov %eax, %ss
	mov $target, %eax
	mov $mb_info, %ebx
	jmp *entry

.org 0x1b8
.byte 0x56, 0x53, 0x4F, 0, 0, 0

.org 0x1be
.space 16, 0

.org 0x1ce
.space 16, 0

.org 0x1de
.space 16, 0

.org 0x1ee,
.space 16, 0

.org 0x1fe
.byte 0x55, 0xaa
