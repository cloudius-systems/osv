#ifndef ELF_HH
#define ELF_HH

#include "fs/fs.hh"
#include <vector>

namespace elf {

    typedef unsigned long u64;
    typedef unsigned int u32;
    typedef unsigned short u16;
    typedef unsigned char u8;
    typedef unsigned long ulong;

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

    enum {
        DT_NULL = 0, // ignored Marks the end of the dynamic array
        DT_NEEDED = 1, // d_val The string table offset of the name of a needed library.Dynamic table 15
        DT_PLTRELSZ = 2, // d_val Total size, in bytes, of the relocation entries associated with
          // the procedure linkage table.
        DT_PLTGOT = 3, // d_ptr Contains an address associated with the linkage table. The
          // speciﬁc meaning of this ﬁeld is processor-dependent.
        DT_HASH = 4, // d_ptr Address of the symbol hash table, described below.
        DT_STRTAB = 5, // d_ptr Address of the dynamic string table.
        DT_SYMTAB = 6, // d_ptr Address of the dynamic symbol table.
        DT_RELA = 7, // d_ptr Address of a relocation table with Elf64_Rela entries.
        DT_RELASZ = 8, // d_val Total size, in bytes, of the DT_RELA relocation table.
        DT_RELAENT = 9, // d_val Size, in bytes, of each DT_RELA relocation entry.
        DT_STRSZ = 10, // d_val Total size, in bytes, of the string table.
        DT_SYMENT = 11, // d_val Size, in bytes, of each symbol table entry.
        DT_INIT = 12, // d_ptr Address of the initialization function.
        DT_FINI = 13, // d_ptr Address of the termination function.
        DT_SONAME = 14, // d_val The string table offset of the name of this shared object.
        DT_RPATH = 15, // d_val The string table offset of a shared library search path string.
        DT_SYMBOLIC = 16, // ignored The presence of this dynamic table entry modiﬁes the
          // symbol resolution algorithm for references within the
          // library. Symbols deﬁned within the library are used to
          // resolve references before the dynamic linker searches the
          // usual search path.
        DT_REL = 17, // d_ptr Address of a relocation table with Elf64_Rel entries.
        DT_RELSZ = 18, // d_val Total size, in bytes, of the DT_REL relocation table.
        DT_RELENT = 19, // d_val Size, in bytes, of each DT_REL relocation entry.
        DT_PLTREL = 20, // d_val Type of relocation entry used for the procedure linkage
          // table. The d_val member contains either DT_REL or DT_RELA.
        DT_DEBUG = 21, // d_ptr Reserved for debugger use.
        DT_TEXTREL = 22, // ignored The presence of this dynamic table entry signals that the
          // relocation table contains relocations for a non-writable
          // segment.
        DT_JMPREL = 23, // d_ptr Address of the relocations associated with the procedure
          // linkage table.
        DT_BIND_NOW = 24, // ignored The presence of this dynamic table entry signals that the
          // dynamic loader should process all relocations for this object
          // before transferring control to the program.
        DT_INIT_ARRAY = 25, // d_ptr Pointer to an array of pointers to initialization functions.
        DT_FINI_ARRAY = 26, // d_ptr Pointer to an array of pointers to termination functions.
        DT_INIT_ARRAYSZ = 27, // d_val Size, in bytes, of the array of initialization functions.
        DT_FINI_ARRAYSZ = 28, // d_val Size, in bytes, of the array of termination functions.
        DT_LOOS = 0x60000000, // Deﬁnes a range of dynamic table tags that are reserved for
          // environment-speciﬁc use.
        DT_HIOS = 0x6FFFFFFF, //
        DT_LOPROC = 0x70000000, // Deﬁnes a range of dynamic table tags that are reserved for
          // processor-speciﬁc use.
        DT_HIPROC = 0x7FFFFFFF, //
    };

    struct Elf64_Dyn {
        Elf64_Sxword d_tag;
        union {
            Elf64_Xword d_val;
            Elf64_Addr d_ptr;
        } d_un;
    };

    class elf_file {
    public:
	explicit elf_file(::file& f);
	void load_segments();
        void set_base(void* base);
    private:
	void load_elf_header();
	void load_program_headers();
	void load_segment(const Elf64_Phdr& phdr);
    private:
	::file& _f;
	Elf64_Ehdr _ehdr;
	std::vector<Elf64_Phdr> _phdrs;
	void* _base;
	Elf64_Dyn* _dynamic_table;
    };
}

void load_elf(file& f, void* addr = reinterpret_cast<void*>(64 << 20));

#endif
