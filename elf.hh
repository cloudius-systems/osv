#ifndef ELF_HH
#define ELF_HH

#include "fs/fs.hh"
#include <vector>

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

    enum {
        PT_NULL = 0, // Unused entry
        PT_LOAD = 1, // Loadable segment
        PT_DYNAMIC = 2, // Dynamic linking tables
        PT_INTERP = 3, // Program interpreter path name
        PT_NOTE = 4, // Note sections
    };

    struct Elf64_Phdr {
	Elf64_Word p_type; /* Type of segment */
	Elf64_Word p_flags; /* Segment attributes */
	Elf64_Off p_offset; /* Offset in file */
	Elf64_Addr p_vaddr; /* Virtual address in memory */
	Elf64_Addr p_paddr; /* Reserved */
	Elf64_Xword p_filesz; /* Size of segment in file */
	Elf64_Xword p_memsz; /* Size of segment in memory */
	Elf64_Xword p_align; /* Alignment of segment */
    };

    class elf_file {
    public:
	explicit elf_file(::file& f);
	void load_segments();
    private:
	void load_elf_header();
	void load_program_headers();
	void load_segment(const Elf64_Phdr& phdr);
    private:
	::file& _f;
	Elf64_Ehdr _ehdr;
	std::vector<Elf64_Phdr> _phdrs;
    };
}

void load_elf(file& f, unsigned long addr = 64 << 20);

#endif
