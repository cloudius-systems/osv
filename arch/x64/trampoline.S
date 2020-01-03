#include "cfi.S"

.text
.code64
.global trampoline
trampoline:
    push %rbp
    mov %rsp, %rbp
    push %rbx
    push %rcx
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    xor %rax, %rax
    mov %r10, %rcx
    VMFUNC
    mov $0x20000000, %rax
    mov %rax, %rsp
    mov $0x100004000, %rcx
    call *%rcx
    mov %rax, %rbx
    xor %rax, %rax
    mov $0, %rcx
    VMFUNC
    mov %rbx, %rax
    mov -0x38(%rbp), %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %rcx
    pop %rbx
    pop %rbp
    ret


