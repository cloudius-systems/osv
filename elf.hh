#ifndef ELF_HH
#define ELF_HH

#include "fs/fs.hh"

namespace elf {

    typedef unsigned long u64;
    typedef unsigned int u32;
    typedef unsigned short u16;
    typedef unsigned char u8;

    typedef u64 Elf64_Addr;
    typedef u64 Elf64_Off;
    typedef u16 Elf64_Half;
    typedef u32 Elf64_Word;
    typedef int Elf64_Sword;
    typedef u64 Elf64_Xword;
    typedef signed long Elf64_Sxword;

    enum {
	EI_MAG0 = 0, // File identiﬁcation
	EI_MAG1 = 1,
	EI_MAG2 = 2,
	EI_MAG3 = 3,
	EI_CLASS = 4, // File class
	EI_DATA = 5, // Data encoding
	EI_VERSION = 6, // File version
	EI_OSABI = 7, // OS/ABI identiﬁcation
	EI_ABIVERSION = 8, // ABI version
	EI_PAD = 9, // Start of padding bytes
	EI_NIDENT = 16, // Size of e_ident[]
    };

    enum {
	ELFCLASS32 = 1, // 32-bit objects
	ELFCLASS64 = 2, // 64-bit objects
    };

    enum {
	ELFDATA2LSB = 1, // Object ﬁle data structures are little-endian
	ELFDATA2MSB = 2, // Object ﬁle data structures are big-endian
    };

    enum {
	EV_CURRENT = 1, // Elf Version
    };

    enum {
	ELFOSABI_SYSV = 0, // System V ABI
	ELFOSABI_HPUX = 1, // HP-UX operating system
	ELFOSABI_LINUX = 3, // Linux
	ELFOSABI_STANDALONE = 255, // Standalone (embedded) application
    };

    struct Elf64_Ehdr {
	unsigned char e_ident[16]; /* ELF identification */
	Elf64_Half e_type; /* Object file type */
	Elf64_Half e_machine; /* Machine type */
	Elf64_Word e_version; /* Object file version */
	Elf64_Addr e_entry; /* Entry point address */
	Elf64_Off e_phoff; /* Program header offset */
	Elf64_Off e_shoff; /* Section header offset */
	Elf64_Word e_flags; /* Processor-specific flags */
	Elf64_Half e_ehsize; /* ELF header size */
	Elf64_Half e_phentsize; /* Size of program header entry */
	Elf64_Half e_phnum; /* Number of program header entries */
	Elf64_Half e_shentsize; /* Size of section header entry */
	Elf64_Half e_shnum; /* Number of section header entries */
	Elf64_Half e_shstrndx; /* Section name string table index */
    };

    class elf_file {
    public:
	explicit elf_file(::file& f);
    private:
	void load_elf_header();
    private:
	::file& _f;
	Elf64_Ehdr _ehdr;
    };
}

void load_elf(file& f, unsigned long addr = 64 << 20);

#endif
