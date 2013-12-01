/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ELF_HH
#define ELF_HH

#include "fs/fs.hh"
#include <vector>
#include <map>
#include <memory>
#include <osv/types.h>
#include <atomic>

/**
 * elf namespace
 */
namespace elf {

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
    PT_PHDR = 6, // program header
    PT_TLS = 7, // Thread local storage initial segment
    PT_GNU_EH_FRAME = 0x6474e550, // Exception handling records
    PT_GNU_STACK = 0x6474e551, // Stack permissions record
    PT_GNU_RELRO = 0x6474e552, // Read-only after relocations
    PT_PAX_FLAGS = 0x65041580,
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
    DT_GNU_HASH = 0x6ffffef5,
};

enum {
    STN_UNDEF = 0,
};

struct Elf64_Dyn {
    Elf64_Sxword d_tag;
    union {
        Elf64_Xword d_val;
        Elf64_Addr d_ptr;
    } d_un;
};

struct Elf64_Rela {
    Elf64_Addr r_offset; /* Address of reference */
    Elf64_Xword r_info; /* Symbol index and type of relocation */
    Elf64_Sxword r_addend; /* Constant part of expression */
};

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

enum {
    STB_LOCAL = 0, //  Not visible outside the object ﬁle
    STB_GLOBAL = 1, // Global symbol, visible to all object ﬁles
    STB_WEAK = 2, // Global scope, but with lower precedence than global symbols
    STB_LOOS = 10, // Environment-speciﬁc use
    STB_HIOS = 12,
    STB_LOPROC = 13, // Processor-speciﬁc use
    STB_HIPROC = 15,
};

enum {
    STT_NOTYPE = 0, // No type speciﬁed (e.g., an absolute symbol)
    STT_OBJECT = 1, // Data object
    STT_FUNC = 2, // Function entry point
    STT_SECTION = 3, // Symbol is associated with a section
    STT_FILE = 4, // Source ﬁle associated with the object ﬁle
    STT_LOOS = 10, // Environment-speciﬁc use
    STT_HIOS = 12,
    STT_LOPROC = 13, // Processor-speciﬁc use
    STT_HIPROC = 15,
    STT_IFUNC = 10, // Indirect function
};

enum {
    SHN_UNDEF = 0, // Used to mark an undeﬁned or meaningless section reference
    SHN_LOPROC = 0xFF00, // Processor-speciﬁc use
    SHN_HIPROC = 0xFF1F,
    SHN_LOOS = 0xFF20, // Environment-speciﬁc use
    SHN_HIOS = 0xFF3F,
    SHN_ABS = 0xFFF1, // Indicates that the corresponding reference is an absolute value
    SHN_COMMON = 0xFFF2, // Indicates a symbol that has been declared as a common block
                         // (Fortran COMMON or C tentative declaration)
};

struct Elf64_Sym {
    Elf64_Word st_name; /* Symbol name */
    unsigned char st_info; /* Type and Binding attributes */
    unsigned char st_other; /* Reserved */
    Elf64_Half st_shndx; /* Section table index */
    Elf64_Addr st_value; /* Symbol value */
    Elf64_Xword st_size; /* Size of object (e.g., common) */
};

class program;
struct symbol_module;

struct tls_data {
    void* start;
    size_t filesize;
    size_t size;
};

struct dladdr_info {
    dladdr_info() : fname{}, base{}, sym{}, addr{} {}
    const char* fname;
    void* base;
    const char* sym;
    void* addr;
};

class object {
public:
    explicit object(program& prog, std::string pathname);
    virtual ~object();
    void load_needed();
    void relocate();
    void set_base(void* base);
    void set_dynamic_table(Elf64_Dyn* dynamic_table);
    void* base() const;
    void* end() const;
    Elf64_Sym* lookup_symbol(const char* name);
    void load_segments();
    void unload_segments();
    void* resolve_pltgot(unsigned index);
    tls_data tls();
    const std::vector<Elf64_Phdr> *phdrs();
    std::string soname();
    std::string pathname();
    void run_init_funcs();
    void run_fini_funcs();
    template <typename T = void>
    T* lookup(const char* name);
    dladdr_info lookup_addr(const void* addr);
    unsigned long module_index() const;
    void* tls_addr();
protected:
    virtual void load_segment(const Elf64_Phdr& segment) = 0;
    virtual void unload_segment(const Elf64_Phdr& segment) = 0;
private:
    Elf64_Sym* lookup_symbol_old(const char* name);
    Elf64_Sym* lookup_symbol_gnu(const char* name);
    template <typename T>
    T* dynamic_ptr(unsigned tag);
    Elf64_Xword dynamic_val(unsigned tag);
    const char* dynamic_str(unsigned tag);
    bool dynamic_exists(unsigned tag);
    std::vector<const char*> dynamic_str_array(unsigned tag);
    Elf64_Dyn& dynamic_tag(unsigned tag);
    Elf64_Dyn* _dynamic_tag(unsigned tag);
    symbol_module symbol(unsigned idx);
    Elf64_Xword symbol_tls_module(unsigned idx);
    void relocate_rela();
    void relocate_pltgot();
    unsigned symtab_len();
protected:
    program& _prog;
    std::string _pathname;
    Elf64_Ehdr _ehdr;
    std::vector<Elf64_Phdr> _phdrs;
    void* _base;
    void* _end;
    void* _tls_segment;
    ulong _tls_init_size, _tls_uninit_size;
    Elf64_Dyn* _dynamic_table;
    unsigned long _module_index;

    // Keep list of references to other modules, to prevent them from being
    // unloaded. When this object is unloaded, the reference count of all
    // objects listed here goes down, and they too may be unloaded.
    std::vector<std::shared_ptr<elf::object>> _needed;
    // TODO: we also need a set<shared_ptr<object>> for other objects used in
    // resolving symbols from this symbol.

    // Allow objects on program->_modules to be usable for the threads
    // currently initializing them, but not yet visible for other threads.
    // This simplifies the code (the initializer can use the regular lookup
    // functions).
private:
    std::atomic<void*> _visibility;
    bool visible(void) const;
public:
    void setprivate(bool);
};

class file : public object {
public:
    explicit file(program& prog, fileref f, std::string pathname);
    virtual ~file();
    void load_program_headers();
    void load_elf_header();
protected:
    virtual void load_segment(const Elf64_Phdr& phdr);
    virtual void unload_segment(const Elf64_Phdr& phdr);
private:
    ::fileref _f;
};

class memory_image : public object {
public:
    explicit memory_image(program& prog, void* base);
protected:
    virtual void load_segment(const Elf64_Phdr& phdr);
    virtual void unload_segment(const Elf64_Phdr& phdr);
};

struct symbol_module {
public:
    symbol_module();
    symbol_module(Elf64_Sym* sym, object* object);
    void* relocated_addr() const;
    Elf64_Sym* symbol;
    object* obj;
};

/**
 * The dynamic linker's view of the running program.
 *
 * \b program provides an interface to the dynamic linking of the running
 * program. It allows loading and unloading shared objects, and looking up
 * symbols.
 *
 * \b program is a singleton, whose only instance can be retrieved with
 * program::get_program().
 *
 * For example, to load a shared object, run its main function, and unload
 * it when no longer in use, you use the following code:
 * \code
 * auto lib = elf::get_program()->get_library(path);
 * auto main = lib->lookup<int (int, char**)>("main");
 * int rc = main(argc, argv);
 * // The shared object may be unloaded when "lib" goes out of scope
 * \endcode
 * This example is missing error handling: see osv::run() for a more complete
 * implementation.
 */
class program {
public:
    explicit program(void* base = reinterpret_cast<void*>(0x100000000000UL));
    /**
     * Load a shared library, and return an interface to it.
     *
     * \b get_library ensures that the specified shared library is loaded, and
     * returns a shared pointer to a elf::object object through which this
     * shared library can be used.
     *
     * For example, the following code snippet runs the main() function of a
     * given shared library:
     * \code
     * auto lib = elf::get_program()->get_library(path);
     * auto main = lib->lookup<int (int, char**)>("main");
     * int rc = main(argc, argv);
     * \endcode
     *
     * \return get_library() returns a reference-counted <I>shared pointer</I>
     * (std::shared_ptr) to an elf::object. When the last shared-pointer
     * pointing to the same library goes out of scope, the library will be
     * automatically unloaded from memory. If the above example, as soon as
     * "lib" goes out of scope, and if no other thread still refers to the same
     * library, the library will be unloaded.
     *
     * \param[in] lib         The shared library to load. Can be a relative or
     *                        absolute pathname, or a module name (to be looked
     *                        up in the library search path.
     * \param[in] extra_path  Additional directories to search in addition to
     *                        the default search path which is set with
     *                        set_search_path().
     */
    std::shared_ptr<elf::object>
    get_library(std::string lib, std::vector<std::string> extra_path = {});

    /**
     * Set the default search path for get_library().
     *
     * The search path defaults (as set in loader.cc) to /, /usr/lib.
     */
    void set_search_path(std::initializer_list<std::string> path);

    symbol_module lookup(const char* symbol);
    template <typename T>
    T* lookup_function(const char* symbol);
    tls_data tls();
    // run a function with all current modules (const std::vector<object*>&) as a parameter
    template <typename functor>
    void with_modules(functor f);
    dladdr_info lookup_addr(const void* addr);
    void* tls_addr(ulong module);
private:
    void add_debugger_obj(object* obj);
    void del_debugger_obj(object* obj);
    void* do_lookup_function(const char* symbol);
    void set_object(std::string lib, std::shared_ptr<object> obj);
    void remove_object(object *obj);
    ulong register_dtv(object* obj);
    void free_dtv(object* obj);
private:
    void* _next_alloc;
    std::shared_ptr<object> _core;
    std::map<std::string, std::weak_ptr<object>> _files;
    std::vector<object*> _modules; // in priority order
    // used to determine object::_module_index, so indexes
    // are stable even when objects are deleted:
    std::vector<object*> _module_index_list;
    std::vector<std::string> _search_path;
    // Count object additions and removals from _modules. dl_iterate_phdr()
    // callbacks can use this to know if the object list has not changed.
    int _modules_adds = 0, _modules_subs = 0;
    // debugger interface
    static object* s_objs[100];

    friend elf::file::~file();
    friend class object;
};

/**
 * Get the single elf::program instance
 */
program* get_program();

struct init_table {
    void (**start)();
    unsigned count;
    tls_data tls;
};

init_table get_init(Elf64_Ehdr* header);

template <class T>
T*
program::lookup_function(const char* symbol)
{
    return reinterpret_cast<T*>(do_lookup_function(symbol));
}

template <typename functor>
inline
void program::with_modules(functor f)
{
    // FIXME: locking?
    const std::vector<object*>& tmp = _modules;
    f(tmp, _modules_adds, _modules_subs);
}

template <>
void* object::lookup(const char* symbol);

template <typename T>
T*
object::lookup(const char* symbol)
{
    return reinterpret_cast<T*>(lookup<void>(symbol));
}

}

#endif
