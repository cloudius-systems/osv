
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
tss_desc:
    .quad 0x0000890000000067 # tss (two entries)
    .quad 0
gdt_end = .

.align 8
. = . + 4  # make sure tss_ist is aligned on a quad boundary

.global tss
.global tss_ist
tss:
	.long 0
	.quad 0, 0, 0
tss_ist:
	.quad 0, interrupt_stack_top, 0, 0, 0, 0, 0, 0
	.long 0, 0, 0

tr:	.word     tss_desc - gdt

.bss

.align 16
. = . + 4096*4
init_stack_top = .

. = . + 4096*10
.global interrupt_stack_top
interrupt_stack_top = .

.text

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
    mov $0x000007b8, %eax
    mov %eax, %cr4
    lea ident_pt_l4, %eax
    mov %eax, %cr3
    mov $0xc0000080, %ecx
    mov $0x00000900, %eax
    xor %edx, %edx
    wrmsr
    mov $0x80010001, %eax
    mov %eax, %cr0
    ljmpl $8, $start64
.code64
start64:
    .cfi_startproc simple
    .cfi_def_cfa %rsp, 0
    .cfi_undefined %rip
    mov $tss, %edx
    movw %dx, tss_desc + 2
    shr $16, %edx
    movb %dl, tss_desc + 4
    movb %dh, tss_desc + 7
    ltr tr
    lea .bss, %rdi
    lea .edata, %rcx
    sub %rdi, %rcx
    xor %eax, %eax
    rep stosb
    mov %rbp, elf_header
    mov %rbx, multiboot_info
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

.align 16
. = . + 4096 * 16
smp_stack_top = .

.data

smp_stack_free: .quad smp_stack_top

.bss
# 'ltr' expects an available TSS, then marks it busy, so only one
# can be loaded at a time
trlock: .byte 0

.text
.code64

smpboot64:
1:
	lock bts $0, trlock
	pause
	jc 1b
	btrl $9, tss_desc+4
	ltr tr
	lock btc $0, trlock
	mov $-4096, %rsp
	lock xadd %rsp, smp_stack_free
	call smp_main
