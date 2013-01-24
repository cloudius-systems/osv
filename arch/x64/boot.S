
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

tss:	.rept 0x67
	.byte 0
	.endr

tr:	.word     tss_desc - gdt

.bss

.align 16
. = . + 4096*4
init_stack_top = .

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

