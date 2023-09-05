#ifndef ARCH_ELF_HH
#define ARCH_ELF_HH

enum {
    R_AARCH64_NONE = 0,
    R_AARCH64_NONE2 = 256,
    R_AARCH64_ABS64 = 257,
    R_AARCH64_COPY = 1024,
    R_AARCH64_GLOB_DAT = 1025,
    R_AARCH64_JUMP_SLOT = 1026,
    R_AARCH64_RELATIVE = 1027,
    R_AARCH64_TLS_DTPREL64 = 1028,
    R_AARCH64_TLS_DTPMOD64 = 1029,
    R_AARCH64_TLS_TPREL64 = 1030,
    R_AARCH64_TLSDESC = 1031,
    R_AARCH64_IRELATIVE = 1032
};

/* for pltgot relocation */
#define ARCH_JUMP_SLOT R_AARCH64_JUMP_SLOT
#define ARCH_TLSDESC R_AARCH64_TLSDESC
#define ARCH_IRELATIVE R_AARCH64_IRELATIVE

#define ELF_KERNEL_MACHINE_TYPE 183

static constexpr unsigned SAFETY_BUFFER = 256;
#include <osv/align.hh>

inline void run_entry_point(void* ep, int argc, char** argv, int argv_size)
{
    //The layout of the stack and state of all relevant registers is similar
    //to how it looks for x86_64. The main difference (possibly for now)
    //is the inlined assembly
    int argc_plus_argv_stack_size = argv_size + 1;

    //Capture current stack pointer
    void *stack;
    asm volatile ("mov %0, sp" : "=r"(stack));

    //The code below puts argv and auxv vector onto the stack but it may
    //also end up using some of the stack. To make sure there is no collision
    //let us leave some space - SAFETY_BUFFER - between current stack pointer
    //and the position on the stack we will be writing to.
    stack -= (SAFETY_BUFFER + argc_plus_argv_stack_size * sizeof(char*));

    //According to the document above the stack pointer should be 16-bytes aligned
    stack = align_down(stack, 16);

    *reinterpret_cast<u64*>(stack) = argc;
    memcpy(stack + sizeof(char*), argv, argv_size * sizeof(char*));

    //Set stack pointer and jump to the ELF entry point
    asm volatile (
        "mov sp, %1\n\t" //set stack
        "blr %0\n\t"
        :
        : "r"(ep), "r"(stack));
}

#endif /* ARCH_ELF_HH */
