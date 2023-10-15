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
#include <stack>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <osv/types.h>
#include <osv/sched.hh>
#include <atomic>

#include "arch-elf.hh"

/// Marks a shared object as locked in memory and forces eager resolution of
/// PLT entries so OSv APIs like preempt_disable() can be used
#define OSV_ELF_MLOCK_OBJECT() asm(".pushsection .note.osv-mlock, \"a\"; .long 0, 0, 0; .popsection")

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
    EI_MAG0 = 0, // File identification
    EI_MAG1 = 1,
    EI_MAG2 = 2,
    EI_MAG3 = 3,
    EI_CLASS = 4, // File class
    EI_DATA = 5, // Data encoding
    EI_VERSION = 6, // File version
    EI_OSABI = 7, // OS/ABI identification
    EI_ABIVERSION = 8, // ABI version
    EI_PAD = 9, // Start of padding bytes
    EI_NIDENT = 16, // Size of e_ident[]
};

enum {
    ET_NONE = 0, // No file type
    ET_REL = 1, // Relocatable file (i.e., .o object)
    ET_EXEC = 2, // Executable file (non relocatable)
    ET_DYN = 3, // Shared object file (shared library or PIE)
    ET_CORE = 4, // Core file
    ET_LOOS = 0xfe00, // operating system specific range
    ET_HIOS = 0xfeff,
    ET_LOPROC = 0xff00, // processor specific range
    ET_HIPROC = 0xffff
};

enum {
    ELFCLASS32 = 1, // 32-bit objects
    ELFCLASS64 = 2, // 64-bit objects
};

enum {
    ELFDATA2LSB = 1, // Object file data structures are little-endian
    ELFDATA2MSB = 2, // Object file data structures are big-endian
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
    PT_GNU_PROPERTY = 0x6474e553,
    PT_PAX_FLAGS = 0x65041580,
};

enum {
    PF_X = 1, // Executable
    PF_W = 2, // Writable
    PF_R = 4, // Readable
    PF_MASKOS = 0x00ff0000, // Environment-specific
    PF_MASKPROC = 0xff000000, // Processor-specific
};

// Note section
enum {
    NT_VERSION = 1,
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

class Elf64_Note {
public:
    explicit Elf64_Note(void *base, char *str);
    std::string n_owner;
    std::string n_value;
    Elf64_Word n_type;
};

enum {
    DT_NULL = 0, // ignored Marks the end of the dynamic array
    DT_NEEDED = 1, // d_val The string table offset of the name of a needed library.Dynamic table 15
    DT_PLTRELSZ = 2, // d_val Total size, in bytes, of the relocation entries associated with
      // the procedure linkage table.
    DT_PLTGOT = 3, // d_ptr Contains an address associated with the linkage table. The
      // specific meaning of this field is processor-dependent.
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
    DT_RPATH = 15, // d_val The string table offset of a shared library search path string (deprecated)
    DT_SYMBOLIC = 16, // ignored The presence of this dynamic table entry modifies the
      // symbol resolution algorithm for references within the
      // library. Symbols defined within the library are used to
      // resolve references before the dynamic linker searches the
      // usual search path.
    DT_REL = 17, // d_ptr Address of a relocation table with Elf64_Rel entries.
    DT_RELSZ = 18, // d_val Total size, in bytes, of the DT_REL relocation table.
    DT_RELENT = 19, // d_val Size, in bytes, of each DT_REL relocation entry.
    DT_PLTREL = 20, // d_val Type of relocation entry used for the procedure linkage
      // table. The d_val member contains either DT_REL or DT_RELA.
    DT_DEBUG = 21, // d_ptr Reserved for debugger use.
    DT_TEXTREL = 22, // The presence of this dynamic table entry signals that the
      // relocation table contains relocations for a non-writable
      // segment.
    DT_JMPREL = 23, // d_ptr Address of the relocations associated with the procedure
      // linkage table.
    DT_BIND_NOW = 24, // The presence of this dynamic table entry signals that the
      // dynamic loader should process all relocations for this object
      // before transferring control to the program.
    DT_INIT_ARRAY = 25, // d_ptr Pointer to an array of pointers to initialization functions.
    DT_FINI_ARRAY = 26, // d_ptr Pointer to an array of pointers to termination functions.
    DT_INIT_ARRAYSZ = 27, // d_val Size, in bytes, of the array of initialization functions.
    DT_FINI_ARRAYSZ = 28, // d_val Size, in bytes, of the array of termination functions.
    DT_RUNPATH = 29, // d_val The string table offset of a shared library search path string.
    DT_FLAGS = 30, // value is various flags, bits from DF_*.
    DT_FLAGS_1 = 0x6ffffffb, // value is various flags, bits from DF_1_*.
    DT_VERSYM = 0x6ffffff0, // d_ptr Address of the version symbol table.
    DT_LOOS = 0x60000000, // Defines a range of dynamic table tags that are reserved for
      // environment-specific use.
    DT_HIOS = 0x6FFFFFFF, //
    DT_LOPROC = 0x70000000, // Defines a range of dynamic table tags that are reserved for
      // processor-specific use.
    DT_HIPROC = 0x7FFFFFFF, //
    DT_GNU_HASH = 0x6ffffef5,
    DT_TLSDESC_PLT = 0x6ffffef6,
    DT_TLSDESC_GOT = 0x6ffffef7,
};

enum {
    DF_BIND_NOW = 0x8,
};
enum {
    DF_1_NOW = 0x1,
    DF_1_PIE = 0x08000000,
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
    STB_LOCAL = 0, //  Not visible outside the object file
    STB_GLOBAL = 1, // Global symbol, visible to all object files
    STB_WEAK = 2, // Global scope, but with lower precedence than global symbols
    STB_LOOS = 10, // Environment-specific use
    STB_HIOS = 12,
    STB_LOPROC = 13, // Processor-specific use
    STB_HIPROC = 15,
};

enum {
    STT_NOTYPE = 0, // No type specified (e.g., an absolute symbol)
    STT_OBJECT = 1, // Data object
    STT_FUNC = 2, // Function entry point
    STT_SECTION = 3, // Symbol is associated with a section
    STT_FILE = 4, // Source file associated with the object file
    STT_LOOS = 10, // Environment-specific use
    STT_HIOS = 12,
    STT_LOPROC = 13, // Processor-specific use
    STT_HIPROC = 15,
    STT_IFUNC = 10, // Indirect function
};

enum {
    SHN_UNDEF = 0, // Used to mark an undefined or meaningless section reference
    SHN_LOPROC = 0xFF00, // Processor-specific use
    SHN_HIPROC = 0xFF1F,
    SHN_LOOS = 0xFF20, // Environment-specific use
    SHN_HIOS = 0xFF3F,
    SHN_ABS = 0xFFF1, // Indicates that the corresponding reference is an absolute value
    SHN_COMMON = 0xFFF2, // Indicates a symbol that has been declared as a common block
                         // (Fortran COMMON or C tentative declaration)
};

enum {
    AT_NULL = 0,
    AT_IGNORE = 1,
    AT_EXECFD = 2,
    AT_PHDR = 3,
    AT_PHENT = 4,
    AT_PHNUM = 5,
    AT_PAGESZ = 6,
    AT_BASE = 7,
    AT_FLAGS = 8,
    AT_ENTRY = 9,
    AT_NOTELF = 10,
    AT_UID = 11,
    AT_EUID = 12,
    AT_GID = 13,
    AT_EGID = 14,
    AT_CLKTCK = 17,
    AT_PLATFORM = 15,
    AT_HWCAP = 16,
    AT_DCACHEBSIZE = 19,
    AT_ICACHEBSIZE = 20,
    AT_UCACHEBSIZE = 21,
    AT_SECURE = 23,
    AT_RANDOM = 25,
    AT_EXECFN = 31,
};

struct Elf64_Sym {
    Elf64_Word st_name; /* Symbol name */
    unsigned char st_info; /* Type and Binding attributes */
    unsigned char st_other; /* Reserved */
    Elf64_Half st_shndx; /* Section table index */
    Elf64_Addr st_value; /* Symbol value */
    Elf64_Xword st_size; /* Size of object (e.g., common) */
};

#ifdef __x86_64__
    constexpr const char *linux_dl_soname = "ld-linux-x86-64.so.2";
#endif
#ifdef __aarch64__
    constexpr const char *linux_dl_soname = "ld-linux-aarch64.so.1";
#endif

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

struct [[gnu::packed]] Elf64_Shdr {
    Elf64_Word sh_name; /* Section name */
    Elf64_Word sh_type; /* Section type */
    Elf64_Xword sh_flags; /* Section attributes */
    Elf64_Addr sh_addr; /* Virtual address in memory */
    Elf64_Off sh_offset; /* Offset in file */
    Elf64_Xword sh_size; /* Size of section */
    Elf64_Word sh_link; /* Link to other section */
    Elf64_Word sh_info; /* Miscellaneous information */
    Elf64_Xword sh_addralign; /* Address alignment boundary */
    Elf64_Xword sh_entsize; /* Size of entries, if section has table */
};

enum VisibilityLevel {
    Public,
    ThreadOnly,
    ThreadAndItsChildren
};

class object: public std::enable_shared_from_this<elf::object> {
public:
    explicit object(program& prog, std::string pathname);
    virtual ~object();
    void load_needed(std::vector<std::shared_ptr<object>>& loaded_objects);
    void unload_needed();
    void relocate();
    void set_base(void* base);
    void set_dynamic_table(Elf64_Dyn* dynamic_table);
    void* base() const;
    void* end() const;
    Elf64_Sym* lookup_symbol(const char* name, bool self_lookup);
    symbol_module lookup_symbol_deep(const char* name);
    void load_segments();
    void process_headers();
    void unload_segments();
    void fix_permissions();
    void* resolve_pltgot(unsigned index);
    const std::vector<Elf64_Phdr> *phdrs();
    std::string soname();
    std::string pathname();
    void run_init_funcs(int argc, char** argv);
    void run_fini_funcs();
    template <typename T = void>
    T* lookup(const char* name);
    void* cached_lookup(const std::string& name);
    dladdr_info lookup_addr(const void* addr);
    bool contains_addr(const void* addr);
    ulong module_index() const;
    void* tls_addr();
    inline char *setup_tls();
    std::vector<Elf64_Shdr> sections();
    std::string section_name(const Elf64_Shdr& shdr);
    std::vector<Elf64_Sym> symbols();
    const char * symbol_name(const Elf64_Sym *);
    void* entry_point() const;
    void init_static_tls();
    size_t initial_tls_size() { return _initial_tls_size; }
    void* initial_tls() { return _initial_tls.get(); }
    void* get_tls_segment() { return _tls_segment; }
    std::vector<ptrdiff_t>& initial_tls_offsets() { return _initial_tls_offsets; }
    // OSv is only "interested" in ELF objects with e_type equal to ET_EXEC or ET_DYN
    // and rejects others (see load_elf_header()). All these can be broken down
    // into five types:
    // - (1) dynamically linked position dependent executable
    // - (2) dynamically linked position independent executable (dynamically linked PIE)
    // - (3) statically linked position dependent executable
    // - (4) statically linked position independent executable (statically linked PIE)
    // - (5) shared library
    // As OSv processes the ELF objects, most of the time it needs to know if given
    // object belongs to a superset of these types - dynamically linked executables,
    // statically linked executables, position dependent object, etc. For this reason
    // the methods below provide a way to make such determination.
    //
    // Is it a position independent code (type 2, 4 or 5)?
    bool is_pic() { return _ehdr.e_type == ET_DYN; }
    // Is it a position independent executable (type 2 or 4)?
    bool is_pie() { return dynamic_exists(DT_FLAGS_1) && (dynamic_val(DT_FLAGS_1) & DF_1_PIE); }
    // Is it a shared library (type 5)?
    bool is_shared_library() { return _ehdr.e_type == ET_DYN && !is_pie(); }
    // Is it a dynamically linked executable (type 1 or 2, determined by presence of PT_INTERP)?
    bool is_dynamically_linked_executable() { return _is_dynamically_linked_executable; }
    // Is it a statically linked executable (type 3 or 4)?
    // Absence of PT_INTERP is not enough to determine it is a statically linked executable
    // as shared libraries also as missing PT_INTERP.
    bool is_statically_linked_executable() { return !_is_dynamically_linked_executable && !is_shared_library(); }
    bool is_linux_dl() { return this->soname() == linux_dl_soname; }
    ulong get_tls_size();
    ulong get_aligned_tls_size();
    void copy_local_tls(void* to_addr);
    void* eh_frame_addr() { return _eh_frame; }
    Elf64_Half headers_count() { return _ehdr.e_phnum; }
    Elf64_Half headers_size() { return _ehdr.e_phentsize; }
    void* headers_start() { return _headers_start; }
protected:
    virtual void load_segment(const Elf64_Phdr& segment) = 0;
    virtual void unload_segment(const Elf64_Phdr& segment) = 0;
    virtual void read(Elf64_Off offset, void* data, size_t len) = 0;
    bool mlocked();
    bool has_non_writable_text_relocations();
    unsigned get_segment_mmap_permissions(const Elf64_Phdr& phdr);
private:
    Elf64_Sym* lookup_symbol_old(const char* name);
    Elf64_Sym* lookup_symbol_gnu(const char* name, bool self_lookup);
    template <typename T>
    T* dynamic_ptr(unsigned tag);
    Elf64_Xword dynamic_val(unsigned tag);
    const char* dynamic_str(unsigned tag);
    bool dynamic_exists(unsigned tag);
    std::vector<const char*> dynamic_str_array(unsigned tag);
    Elf64_Dyn& dynamic_tag(unsigned tag);
    Elf64_Dyn* _dynamic_tag(unsigned tag);
    symbol_module symbol(unsigned idx, bool ignore_missing = false);
    symbol_module symbol_other(unsigned idx);
    Elf64_Xword symbol_tls_module(unsigned idx);
    void relocate_rela();
    void relocate_pltgot();
    unsigned symtab_len();
    void collect_dependencies(std::unordered_set<elf::object*>& ds);
    std::deque<elf::object*> collect_dependencies_bfs();
    void prepare_initial_tls(void* buffer, size_t size, std::vector<ptrdiff_t>& offsets);
    void prepare_local_tls(std::vector<ptrdiff_t>& offsets);
    void alloc_static_tls();
    void make_text_writable(bool flag);
protected:
    program& _prog;
    std::string _pathname;
    Elf64_Ehdr _ehdr;
    std::vector<Elf64_Phdr> _phdrs;
    void* _base;
    void* _end;
    void* _tls_segment;
    ulong _tls_init_size, _tls_uninit_size, _tls_alignment;
    bool _static_tls;
    ptrdiff_t _static_tls_offset;
    static std::atomic<ptrdiff_t> _static_tls_alloc;
    // _initial_tls is a prepared static TLS template for this object and all
    // its dependencies.
    std::unique_ptr<char[]> _initial_tls;
    size_t _initial_tls_size;
    std::vector<ptrdiff_t> _initial_tls_offsets;
    Elf64_Dyn* _dynamic_table;
    ulong _module_index;
    std::unique_ptr<char[]> _section_names_cache;
    bool _is_dynamically_linked_executable;
    bool is_core();
    bool _init_called;
    void* _eh_frame;
    void* _headers_start;

    std::unordered_map<std::string,void*> _cached_symbols;

    // Keep list of references to other modules, to prevent them from being
    // unloaded. When this object is unloaded, the reference count of all
    // objects listed here goes down, and they too may be unloaded.
    std::vector<std::shared_ptr<elf::object>> _needed;
    std::unordered_set<std::shared_ptr<elf::object>> _used_by_resolve_plt_got;
    mutex _used_by_resolve_plt_got_mutex;
    // Allow objects on program->_modules to be usable for the threads
    // currently initializing them, but not yet visible for other threads.
    // This simplifies the code (the initializer can use the regular lookup
    // functions).

protected:
    // arch-specific relocations
    // ADDR is written to, based on the input params and the object state.
    // The return value is true on success, false on failure.
    bool arch_relocate_rela(u32 type, u32 sym, void *addr,
                            Elf64_Sxword addend);
    bool arch_relocate_jump_slot(symbol_module& sym, void *addr, Elf64_Sxword addend);
    void arch_relocate_tls_desc(u32 sym, void *addr, Elf64_Sxword addend);
    size_t static_tls_end() {
        if (is_core() || _is_dynamically_linked_executable) {
            return 0;
        }
        return _static_tls_offset + get_tls_size();
    }
    size_t static_tls_offset() { return _static_tls_offset; }
private:
    std::atomic<void*> _visibility_thread;
    std::atomic<VisibilityLevel> _visibility_level;
    bool visible(void) const;
public:
    void set_visibility(VisibilityLevel);
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
    virtual void read(Elf64_Off offset, void* data, size_t size) override;
private:
    ::fileref _f;
};

class memory_image : public object {
public:
    explicit memory_image(program& prog, void* base);
protected:
    virtual void load_segment(const Elf64_Phdr& phdr);
    virtual void unload_segment(const Elf64_Phdr& phdr);
    virtual void read(Elf64_Off offset, void* data, size_t size) override;
};

struct symbol_module {
public:
    symbol_module();
    symbol_module(Elf64_Sym* sym, object* object);
    void* relocated_addr() const;
    u64 size() const;

    Elf64_Sym* symbol;
    object* obj;
};


constexpr uintptr_t program_base = 0x100000000000UL;

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
    static const ulong core_module_index;

    explicit program(void* base = reinterpret_cast<void*>(program_base));
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
     * \param[in] delay_init  If true the init functions in the library and its
     *                        dependencies will not be executed until some later
     *                        time when the init_library() is called. By default
     *                        the init functions are executed right away.
     */
    std::shared_ptr<elf::object>
    get_library(std::string lib, std::vector<std::string> extra_path = {}, bool delay_init = false);

    /**
     * Execute init functions of the library itself and its dependencies.
     *
     * Any arguments passed in are relayed to the init functions. Right now
     * the only place that explicitly invokes init_library is application::main()
     * method which also passes any argv passed to the application.
     */
    void init_library(int argc = 0, char **argv = nullptr);

    /**
     * Set the default search path for get_library().
     *
     * The search path defaults (as set in elf::program::program())
     * to /, /usr/lib.
     */
    void set_search_path(std::initializer_list<std::string> path);

    symbol_module lookup(const char* symbol, object* seeker);
    symbol_module lookup_next(const char* name, const void* retaddr);
    template <typename T>
    T* lookup_function(const char* symbol);

    struct modules_list {
        // List of objects, in search priority order
        std::vector<object*> objects;
        // Count object additions and removals from _modules. dl_iterate_phdr
        // callbacks can use this to know if the object list has not changed.
        int adds = 0, subs = 0;
    };
    /**
     * Safely run a function object with the current list of modules.
     *
     * The given function is run with a const elf::program::modules_list&
     * parameter. The function is free to safely use the shared libraries
     * on the list given to it, because with_modules() guarantees that none
     * of them can get deleted until with_modules() call completes.
     */
    template <typename functor>
    void with_modules(functor f);
    dladdr_info lookup_addr(const void* addr);
    elf::object *object_containing_addr(const void *addr);
    inline object *tls_object(ulong module);
    void *get_libvdso_base() { return _libvdso->base(); }
private:
    void add_debugger_obj(object* obj);
    void del_debugger_obj(object* obj);
    void* do_lookup_function(const char* symbol);
    void remove_object(object *obj);
    ulong register_dtv(object* obj);
    void free_dtv(object* obj);
    std::shared_ptr<object> load_object(std::string name,
            std::vector<std::string> extra_path,
            std::vector<std::shared_ptr<object>> &loaded_objects);
    void initialize_libvdso();
private:
    mutex _mutex;
    void* _next_alloc;
    std::shared_ptr<object> _core;
    std::shared_ptr<object> _libvdso;
    std::map<std::string, std::weak_ptr<object>> _files;
    // used to determine object::_module_index, so indexes
    // are stable even when objects are deleted:
    osv::rcu_ptr<std::vector<object*>> _module_index_list_rcu;
    mutex _module_index_list_mutex;
    std::vector<std::string> _search_path;
    osv::rcu_ptr<modules_list> _modules_rcu;
    modules_list modules_get() const;

    // If _module_delete_disable > 0, objects are not deleted but rather
    // collected for deletion when _modules_delete_disable becomes 0.
    mutex _modules_delete_mutex;
    int _module_delete_disable = 0;
    void module_delete_disable();
    void module_delete_enable();
    std::vector <object*> _modules_to_delete;

    // debugger interface
    static std::vector<object*> s_objs;
    static mutex s_objs_mutex;

    friend elf::file::~file();
    friend class object;
    // this allows the objects resolved by get_library() get initialized
    // by init_library() at arbitrary time later - the delayed initialization scenario
    std::stack<std::vector<std::shared_ptr<object>>> _loaded_objects_stack;
};

extern void *missing_symbols_page_addr;
void setup_missing_symbols_detector();

void create_main_program();

/**
 * Get the current elf::program instance
 */
program* get_program();

/* tables needed for initial dynamic segment relocations */
struct init_dyn_tabs {
    const Elf64_Sym *symtab;
    const Elf64_Word *hashtab;
    const char *strtab;
    const Elf64_Sym *lookup(u32 sym);
};

struct init_table {
    void (**start)();
    unsigned count;
    tls_data tls;

    struct init_dyn_tabs dyn_tabs;
};

/* arch-specific relocation for initial dynamic segment relocations */
bool arch_init_reloc_dyn(struct init_table *t, u32 type, u32 sym,
                         void *addr, void *base, Elf64_Sxword addend);

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
    module_delete_disable();
    f(modules_get());
    module_delete_enable();
}

template <>
void* object::lookup(const char* symbol);

template <typename T>
T*
object::lookup(const char* symbol)
{
    return reinterpret_cast<T*>(lookup<void>(symbol));
}

object *program::tls_object(ulong module)
{
#if CONF_lazy_stack_invariant
    assert(sched::preemptable() && arch::irq_enabled());
#endif
#if CONF_lazy_stack
    arch::ensure_next_stack_page();
#endif
    SCOPE_LOCK(osv::rcu_read_lock);
    return (*(get_program()->_module_index_list_rcu.read()))[module];
}

}

#endif
