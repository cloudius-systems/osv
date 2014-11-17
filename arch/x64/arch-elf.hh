#ifndef ARCH_ELF_HH
#define ARCH_ELF_HH

enum {
    R_X86_64_NONE = 0, //  none none
    R_X86_64_64 = 1, //  word64 S + A
    R_X86_64_PC32 = 2, //  word32 S + A - P
    R_X86_64_GOT32 = 3, //  word32 G + A
    R_X86_64_PLT32 = 4, //  word32 L + A - P
    R_X86_64_COPY = 5, //  none none
    R_X86_64_GLOB_DAT = 6, //  word64 S
    R_X86_64_JUMP_SLOT = 7, //  word64 S
    R_X86_64_RELATIVE = 8, //  word64 B + A
    R_X86_64_GOTPCREL = 9, //  word32 G + GOT + A - P
    R_X86_64_32 = 10, //  word32 S + A
    R_X86_64_32S = 11, //  word32 S + A
    R_X86_64_16 = 12, //  word16 S + A
    R_X86_64_PC16 = 13, //  word16 S + A - P
    R_X86_64_8 = 14, //  word8 S + A
    R_X86_64_PC8 = 15, //  word8 S + A - P
    R_X86_64_DPTMOD64 = 16, //  word64
    R_X86_64_DTPOFF64 = 17, //  word64
    R_X86_64_TPOFF64 = 18, //  word64
    R_X86_64_TLSGD = 19, //  word32
    R_X86_64_TLSLD = 20, //  word32
    R_X86_64_DTPOFF32 = 21, //  word32
    R_X86_64_GOTTPOFF = 22, //  word32
    R_X86_64_TPOFF32 = 23, //  word32
    R_X86_64_PC64 = 24, //  word64 S + A - P
    R_X86_64_GOTOFF64 = 25, //  word64 S + A - GOT
    R_X86_64_GOTPC32 = 26, //  word32 GOT + A - P
    R_X86_64_SIZE32 = 32, //  word32 Z + A
    R_X86_64_SIZE64 = 33, //  word64 Z + A
    R_X86_64_IRELATIVE = 37, //  word64 indirect(B + A)
};

/* for pltgot relocation */
#define ARCH_JUMP_SLOT R_X86_64_JUMP_SLOT

inline void elf_entry_point(void* ep)
{
    asm volatile (
        "pushq $0\n\t" // Zero
        "pushq $0\n\t" // Environment pointers
        "pushq $0\n\t" // Zero
        "pushq $0\n\t" // Argument count
        "jmpq  *%0\n\t"
        :
        : "r"(ep));
}

#endif /* ARCH_ELF_HH */
