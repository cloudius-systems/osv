/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "elf.hh"
#include "mmu.hh"
#include <boost/format.hpp>
#include <exception>
#include <memory>
#include <string.h>
#include "align.hh"
#include "debug.hh"
#include <stdlib.h>
#include <unistd.h>
#include <boost/algorithm/string.hpp>
#include <boost/range/algorithm/find.hpp>
#include <functional>
#include <cxxabi.h>
#include <iterator>
#include <sched.hh>

using namespace std;
using namespace boost::range;

namespace {
    typedef boost::format fmt;
}

namespace elf {

const ulong program::core_module_index = 0;

namespace {

unsigned symbol_type(Elf64_Sym& sym)
{
    return sym.st_info & 15;
}

unsigned symbol_binding(Elf64_Sym& sym)
{
    return sym.st_info >> 4;
}

}

symbol_module::symbol_module()
    : symbol()
    , obj()
{
}

symbol_module::symbol_module(Elf64_Sym* _sym, object* _obj)
    : symbol(_sym)
    , obj(_obj)
{
}

void* symbol_module::relocated_addr() const
{
    void* base = obj->base();
    if (symbol->st_shndx == SHN_UNDEF || symbol->st_shndx == SHN_ABS) {
        base = 0;
    }
    switch (symbol_type(*symbol)) {
    case STT_NOTYPE:
        return reinterpret_cast<void*>(symbol->st_value);
        break;
    case STT_OBJECT:
    case STT_FUNC:
        return base + symbol->st_value;
        break;
    case STT_IFUNC:
        return reinterpret_cast<void *(*)()>(base + symbol->st_value)();
    default:
        debug(fmt("unknown relocation type %d\n") % symbol_type(*symbol));
        abort();
    }
}

object::object(program& prog, std::string pathname)
    : _prog(prog)
    , _pathname(pathname)
    , _tls_segment()
    , _tls_init_size()
    , _tls_uninit_size()
    , _dynamic_table(nullptr)
    , _module_index(_prog.register_dtv(this))
    , _visibility(nullptr)
{
}

object::~object()
{
    _prog.free_dtv(this);
}

ulong object::module_index() const
{
    return _module_index;
}

bool object::visible(void) const
{
    auto v = _visibility.load(std::memory_order_acquire);
    return (v == nullptr) || (v == sched::thread::current());
}

void object::setprivate(bool priv)
{
     _visibility.store(priv ? sched::thread::current() : nullptr,
             std::memory_order_release);
}


template <>
void* object::lookup(const char* symbol)
{
    symbol_module sm{lookup_symbol(symbol), this};
    if (!sm.symbol || sm.symbol->st_shndx == SHN_UNDEF) {
        return nullptr;
    }
    return sm.relocated_addr();
}

file::file(program& prog, ::fileref f, std::string pathname)
: object(prog, pathname)
    , _f(f)
{
load_elf_header();
load_program_headers();
}

file::~file()
{
    get_program()->remove_object(this);
}

memory_image::memory_image(program& prog, void* base)
    : object(prog, "")
{
    _ehdr = *static_cast<Elf64_Ehdr*>(base);
    auto p = static_cast<Elf64_Phdr*>(base + _ehdr.e_phoff);
    assert(_ehdr.e_phentsize == sizeof(*p));
    _phdrs.assign(p, p + _ehdr.e_phnum);
    set_base(base);
}

void memory_image::load_segment(const Elf64_Phdr& phdr)
{
}

void memory_image::unload_segment(const Elf64_Phdr& phdr)
{
}

void file::load_elf_header()
{
    read(_f, &_ehdr, 0, sizeof(_ehdr));
    if (!(_ehdr.e_ident[EI_MAG0] == '\x7f'
          && _ehdr.e_ident[EI_MAG1] == 'E'
          && _ehdr.e_ident[EI_MAG2] == 'L'
          && _ehdr.e_ident[EI_MAG3] == 'F')) {
        throw std::runtime_error("bad elf header");
    }
    if (!(_ehdr.e_ident[EI_CLASS] == ELFCLASS64)) {
        throw std::runtime_error("bad elf class");
    }
    if (!(_ehdr.e_ident[EI_DATA] == ELFDATA2LSB)) {
        throw std::runtime_error("bad elf endianness");
    }
    if (!(_ehdr.e_ident[EI_VERSION] == EV_CURRENT)) {
        throw std::runtime_error("bad elf version");
    }
    if (!(_ehdr.e_ident[EI_OSABI] == ELFOSABI_LINUX
          || _ehdr.e_ident[EI_OSABI] == 0)) {
        throw std::runtime_error("bad os abi");
    }
}

namespace {

void* align(void* addr, ulong align, ulong offset)
{
    return align_up(addr - offset, align) + offset;
}

}

void object::set_base(void* base)
{
    auto p = std::min_element(_phdrs.begin(), _phdrs.end(),
                              [](Elf64_Phdr a, Elf64_Phdr b)
                                  { return a.p_type == PT_LOAD
                                        && a.p_vaddr < b.p_vaddr; });
    _base = align(base, p->p_align, p->p_vaddr & (p->p_align - 1)) - p->p_vaddr;
    auto q = std::min_element(_phdrs.begin(), _phdrs.end(),
                              [](Elf64_Phdr a, Elf64_Phdr b)
                                  { return a.p_type == PT_LOAD
                                        && a.p_vaddr > b.p_vaddr; });
    _end = _base + q->p_vaddr + q->p_memsz;
}

void* object::base() const
{
    return _base;
}

void* object::end() const
{
    return _end;
}

void file::load_program_headers()
{
    _phdrs.resize(_ehdr.e_phnum);
    for (unsigned i = 0; i < _ehdr.e_phnum; ++i) {
        read(_f, &_phdrs[i],
            _ehdr.e_phoff + i * _ehdr.e_phentsize,
            _ehdr.e_phentsize);
    }
}

namespace {

ulong page_size = 4096;

}

void file::load_segment(const Elf64_Phdr& phdr)
{
    ulong vstart = align_down(phdr.p_vaddr, page_size);
    ulong filesz_unaligned = phdr.p_vaddr + phdr.p_filesz - vstart;
    ulong filesz = align_up(filesz_unaligned, page_size);
    ulong memsz = align_up(phdr.p_vaddr + phdr.p_memsz, page_size) - vstart;
    mmu::map_file(_base + vstart, filesz, mmu::mmap_fixed, mmu::perm_rwx,
                  _f, align_down(phdr.p_offset, page_size));
    memset(_base + vstart + filesz_unaligned, 0, filesz - filesz_unaligned);
    mmu::map_anon(_base + vstart + filesz, memsz - filesz, mmu::mmap_fixed, mmu::perm_rwx);
}

void object::load_segments()
{
    for (unsigned i = 0; i < _ehdr.e_phnum; ++i) {
        auto &phdr = _phdrs[i];
        switch (phdr.p_type) {
        case PT_NULL:
            break;
        case PT_LOAD:
            load_segment(phdr);
            break;
        case PT_DYNAMIC:
            _dynamic_table = reinterpret_cast<Elf64_Dyn*>(_base + phdr.p_vaddr);
            break;
        case PT_INTERP:
        case PT_NOTE:
        case PT_PHDR:
        case PT_GNU_STACK:
        case PT_GNU_RELRO:
        case PT_GNU_EH_FRAME:
        case PT_PAX_FLAGS:
            break;
        case PT_TLS:
            _tls_segment = _base + phdr.p_vaddr;
            _tls_init_size = phdr.p_filesz;
            _tls_uninit_size = phdr.p_memsz - phdr.p_filesz;
            break;
        default:
            abort();
            throw std::runtime_error("bad p_type");
        }
    }
}

void file::unload_segment(const Elf64_Phdr& phdr)
{
    ulong vstart = align_down(phdr.p_vaddr, page_size);
    ulong filesz_unaligned = phdr.p_vaddr + phdr.p_filesz - vstart;
    ulong filesz = align_up(filesz_unaligned, page_size);
    ulong memsz = align_up(phdr.p_vaddr + phdr.p_memsz, page_size) - vstart;
    mmu::unmap(_base + vstart, filesz);
    mmu::unmap(_base + vstart + filesz, memsz - filesz);
}

void object::unload_segments()
{
    for (unsigned i = 0; i < _ehdr.e_phnum; ++i) {
        auto &phdr = _phdrs[i];
        switch (phdr.p_type) {
        case PT_LOAD:
            unload_segment(phdr);
            break;
        default:
            break;
        }
     }
}

template <typename T>
T* object::dynamic_ptr(unsigned tag)
{
    return static_cast<T*>(_base + dynamic_tag(tag).d_un.d_ptr);
}

Elf64_Xword object::dynamic_val(unsigned tag)
{
    return dynamic_tag(tag).d_un.d_val;
}

const char* object::dynamic_str(unsigned tag)
{
    return dynamic_ptr<const char>(DT_STRTAB) + dynamic_val(tag);
}

bool object::dynamic_exists(unsigned tag)
{
    return _dynamic_tag(tag);
}

Elf64_Dyn* object::_dynamic_tag(unsigned tag)
{
    for (auto p = _dynamic_table; p->d_tag != DT_NULL; ++p) {
        if (p->d_tag == tag) {
            return p;
        }
    }
    return nullptr;
}

Elf64_Dyn& object::dynamic_tag(unsigned tag)
{
    auto r = _dynamic_tag(tag);
    if (!r) {
        throw std::runtime_error("missing tag");
    }
    return *r;
}

std::vector<const char *>
object::dynamic_str_array(unsigned tag)
{
    auto strtab = dynamic_ptr<const char>(DT_STRTAB);
    std::vector<const char *> r;
    for (auto p = _dynamic_table; p->d_tag != DT_NULL; ++p) {
        if (p->d_tag == tag) {
            r.push_back(strtab + p->d_un.d_val);
        }
    }
    return r;
}

static std::string demangle(const char *name) {
    int status;
    char *demangled = abi::__cxa_demangle(name, nullptr, 0, &status);
    std::string ret(name);
    if (demangled) {
        ret += " (";
        ret += demangled;
        ret += ")";
        // "demangled" was allocated with malloc() by __cxa_demangle
        free(demangled);
    }
    return ret;
}

symbol_module object::symbol(unsigned idx)
{
    auto symtab = dynamic_ptr<Elf64_Sym>(DT_SYMTAB);
    assert(dynamic_val(DT_SYMENT) == sizeof(Elf64_Sym));
    auto sym = &symtab[idx];
    auto nameidx = sym->st_name;
    auto name = dynamic_ptr<const char>(DT_STRTAB) + nameidx;
    auto ret = _prog.lookup(name);
    auto binding = sym->st_info >> 4;
    if (!ret.symbol && binding == STB_WEAK) {
        return symbol_module(sym, this);
    }
    if (!ret.symbol) {
        debug(fmt("failed looking up symbol %1%\n") % demangle(name));
        abort();
    }
    return ret;
}

void object::relocate_rela()
{
    auto rela = dynamic_ptr<Elf64_Rela>(DT_RELA);
    assert(dynamic_val(DT_RELAENT) == sizeof(Elf64_Rela));
    unsigned nb = dynamic_val(DT_RELASZ) / sizeof(Elf64_Rela);
    for (auto p = rela; p < rela + nb; ++p) {
        auto info = p->r_info;
        u32 sym = info >> 32;
        u32 type = info & 0xffffffff;
        void *addr = _base + p->r_offset;
        auto addend = p->r_addend;
        switch (type) {
        case R_X86_64_NONE:
            break;
        case R_X86_64_64:
            *static_cast<void**>(addr) = symbol(sym).relocated_addr() + addend;
            break;
        case R_X86_64_RELATIVE:
            *static_cast<void**>(addr) = _base + addend;
            break;
        case R_X86_64_JUMP_SLOT:
        case R_X86_64_GLOB_DAT:
            *static_cast<void**>(addr) = symbol(sym).relocated_addr();
            break;
        case R_X86_64_DPTMOD64:
            if (sym == STN_UNDEF) {
                *static_cast<u64*>(addr) = 0;
            } else {
                *static_cast<u64*>(addr) = symbol(sym).obj->_module_index;
            }
            break;
        case R_X86_64_DTPOFF64:
            *static_cast<u64*>(addr) = symbol(sym).symbol->st_value;
            break;
        case R_X86_64_TPOFF64:
            *static_cast<u64*>(addr) = symbol(sym).symbol->st_value - tls_data().size;
            break;
        default:
            debug("unknown relocation type %d\n", type);
            abort();
        }
    }
}

extern "C" { void __elf_resolve_pltgot(void); }

void object::relocate_pltgot()
{
    auto pltgot = dynamic_ptr<void*>(DT_PLTGOT);
    void *original_plt = nullptr;
    if (pltgot[1]) {
        // The library was prelinked before being copied to OSV, so the
        // .GOT.PLT entries point not to the .PLT but to a prelinked address
        // of the actual function. We'll need to return the link to the .PLT.
        // The prelinker saved us in pltgot[1] the address of .plt + 0x16.
        original_plt = static_cast<void*>(_base + (u64)pltgot[1]);
    }

    auto rel = dynamic_ptr<Elf64_Rela>(DT_JMPREL);
    auto nrel = dynamic_val(DT_PLTRELSZ) / sizeof(*rel);
    for (auto p = rel; p < rel + nrel; ++p) {
        auto info = p->r_info;
          u32 type = info & 0xffffffff;
          assert(type == R_X86_64_JUMP_SLOT);
          void *addr = _base + p->r_offset;
          if (original_plt) {
              // Restore the link to the original plt.
              // We know the JUMP_SLOT entries are in plt order, and that
              // each plt entry is 16 bytes.
              *static_cast<void**>(addr) = original_plt + (p-rel)*16;
          } else {
              // The JUMP_SLOT entry already points back to the PLT, just
              // make sure it is relocated relative to the object base.
              *static_cast<u64*>(addr) += reinterpret_cast<u64>(_base);
          }
    }
    // PLTGOT resolution has a special calling convention, with the symbol
    // index and some word pushed on the stack, so we need an assembly
    // stub to convert it back to the standard calling convention.
    pltgot[1] = this;
    pltgot[2] = reinterpret_cast<void*>(__elf_resolve_pltgot);
}

void* object::resolve_pltgot(unsigned index)
{
    auto rel = dynamic_ptr<Elf64_Rela>(DT_JMPREL);
    auto slot = rel[index];
    auto info = slot.r_info;
    u32 sym = info >> 32;
    u32 type = info & 0xffffffff;
    assert(type == R_X86_64_JUMP_SLOT);
    void *addr = _base + slot.r_offset;
    auto ret = *static_cast<void**>(addr) = symbol(sym).relocated_addr();
    return ret;
}

void object::relocate()
{
    assert(!dynamic_exists(DT_REL));
    if (dynamic_exists(DT_RELA)) {
        relocate_rela();
    }
    if (dynamic_exists(DT_JMPREL)) {
        relocate_pltgot();
    }
}

unsigned long
elf64_hash(const char *name)
{
    unsigned long h = 0, g;
    while (*name) {
        h = (h << 4) + (unsigned char)(*name++);
        if ((g = h & 0xf0000000)) {
            h ^= g >> 24;
        }
        h  &=  0x0fffffff;
    }
    return h;
}

Elf64_Sym* object::lookup_symbol_old(const char* name)
{
    auto symtab = dynamic_ptr<Elf64_Sym>(DT_SYMTAB);
    auto strtab = dynamic_ptr<char>(DT_STRTAB);
    auto hashtab = dynamic_ptr<Elf64_Word>(DT_HASH);
    auto nbucket = hashtab[0];
    auto buckets = hashtab + 2;
    auto chain = buckets + nbucket;
    for (auto ent = buckets[elf64_hash(name) % nbucket];
            ent != STN_UNDEF;
            ent = chain[ent]) {
        auto &sym = symtab[ent];
        if (strcmp(name, &strtab[sym.st_name]) == 0) {
            return &sym;
        }
    }
    return nullptr;
}

uint_fast32_t
dl_new_hash(const char *s)
{
    uint_fast32_t h = 5381;
    for (unsigned char c = *s; c != '\0'; c = *++s) {
        h = h * 33 + c;
    }
    return h & 0xffffffff;
}

Elf64_Sym* object::lookup_symbol_gnu(const char* name)
{
    auto symtab = dynamic_ptr<Elf64_Sym>(DT_SYMTAB);
    auto strtab = dynamic_ptr<char>(DT_STRTAB);
    auto hashtab = dynamic_ptr<Elf64_Word>(DT_GNU_HASH);
    auto nbucket = hashtab[0];
    auto symndx = hashtab[1];
    auto maskwords = hashtab[2];
    auto shift2 = hashtab[3];
    auto bloom = reinterpret_cast<const Elf64_Xword*>(hashtab + 4);
    auto C = sizeof(*bloom) * 8;
    auto hashval = dl_new_hash(name);
    auto bword = bloom[(hashval / C) % maskwords];
    auto hashbit1 = hashval % C;
    auto hashbit2 = (hashval >> shift2) % C;
    if ((bword >> hashbit1) == 0 || (bword >> hashbit2) == 0) {
        return nullptr;
    }
    auto buckets = reinterpret_cast<const Elf64_Word*>(bloom + maskwords);
    auto chains = buckets + nbucket - symndx;
    auto idx = buckets[hashval % nbucket];
    if (idx == 0) {
        return nullptr;
    }
    do {
        if ((chains[idx] & ~1) != (hashval & ~1)) {
            continue;
        }
        if (strcmp(&strtab[symtab[idx].st_name], name) == 0) {
            return &symtab[idx];
        }
    } while ((chains[idx++] & 1) == 0);
    return nullptr;
}

Elf64_Sym* object::lookup_symbol(const char* name)
{
    if (!visible()) {
        return nullptr;
    }
    Elf64_Sym* sym;
    if (dynamic_exists(DT_GNU_HASH)) {
        sym = lookup_symbol_gnu(name);
    } else {
        sym = lookup_symbol_old(name);
    }
    if (sym && sym->st_shndx == SHN_UNDEF) {
        sym = nullptr;
    }
    return sym;
}

unsigned object::symtab_len()
{
    if (dynamic_exists(DT_HASH)) {
        auto hashtab = dynamic_ptr<Elf64_Word>(DT_HASH);
        return hashtab[1];
    }
    auto hashtab = dynamic_ptr<Elf64_Word>(DT_GNU_HASH);
    auto nbucket = hashtab[0];
    auto symndx = hashtab[1];
    auto maskwords = hashtab[2];
    auto bloom = reinterpret_cast<const Elf64_Xword*>(hashtab + 4);
    auto buckets = reinterpret_cast<const Elf64_Word*>(bloom + maskwords);
    auto chains = buckets + nbucket - symndx;
    unsigned len = 0;
    for (unsigned b = 0; b < nbucket; ++b) {
        auto idx = buckets[b];
        if (idx == 0) {
            continue;
        }
        do {
            ++len;
        } while ((chains[idx++] & 1) == 0);
    }
    return len;
}

dladdr_info object::lookup_addr(const void* addr)
{
    dladdr_info ret;
    if (addr < _base || addr >= _end) {
        return ret;
    }
    auto strtab = dynamic_ptr<char>(DT_STRTAB);
    auto symtab = dynamic_ptr<Elf64_Sym>(DT_SYMTAB);
    auto len = symtab_len();
    symbol_module best;
    for (unsigned i = 1; i < len; ++i) {
        auto& sym = symtab[i];
        auto type = sym.st_info & 15;
        if (type != STT_OBJECT && type != STT_FUNC) {
            continue;
        }
        auto bind = (sym.st_info >> 4) & 15;
        if (bind != STB_GLOBAL && bind != STB_WEAK) {
            continue;
        }
        symbol_module sm{&sym, this};
        auto s_addr = sm.relocated_addr();
        if (s_addr < addr) {
            continue;
        }
        if (!best.symbol || sm.relocated_addr() < best.relocated_addr()) {
            best = sm;
        }
    }
    if (!best.symbol) {
        return ret;
    }
    ret.fname = _pathname.c_str();
    ret.base = _base;
    ret.sym = strtab + best.symbol->st_name;
    ret.addr = best.relocated_addr();
    return ret;
}

static std::string dirname(std::string path)
{
    auto pos = path.rfind('/');
    if (pos == path.npos) {
        return "/";
    }
    return path.substr(0, pos);
}

void object::load_needed()
{
    std::vector<std::string> rpath;
    if (dynamic_exists(DT_RPATH)) {
        std::string rpath_str = dynamic_str(DT_RPATH);
        boost::replace_all(rpath_str, "$ORIGIN", dirname(_pathname));
        boost::split(rpath, rpath_str, boost::is_any_of(":"));
    }
    auto needed = dynamic_str_array(DT_NEEDED);
    for (auto lib : needed) {
        auto obj = _prog.get_library(lib, rpath);
        if (obj) {
            // Keep a reference to the needed object, so it won't be
            // unloaded until this object is unloaded.
            _needed.push_back(std::move(obj));
        } else {
            debug("could not load %s\n", lib);
        }
    }
}

void object::unload_needed()
{
    _needed.clear();
}

tls_data object::tls()
{
    return tls_data{_tls_segment, _tls_init_size + _tls_uninit_size};
}

std::string object::soname()
{
    return dynamic_exists(DT_SONAME) ? dynamic_str(DT_SONAME) : std::string();
}

const std::vector<Elf64_Phdr> *object::phdrs()
{
    return &_phdrs;
}

std::string object::pathname()
{
    return _pathname;
}

// Run the object's static constructors or similar initialization
void object::run_init_funcs()
{
    // FIXME: may also need to run DT_INIT here (before DT_INIT_ARRAY), but
    // on modern gcc it seems it doesn't work (and isn't needed).
    if (dynamic_exists(DT_INIT_ARRAY)) {
        auto funcs = dynamic_ptr<void (*)()>(DT_INIT_ARRAY);
        auto nr = dynamic_val(DT_INIT_ARRAYSZ) / sizeof(*funcs);
        for (auto i = 0u; i < nr; ++i) {
            funcs[i]();
        }
    }
}

// Run the object's static destructors or similar finalization
void object::run_fini_funcs()
{
    if (dynamic_exists(DT_FINI_ARRAY)) {
        auto funcs = dynamic_ptr<void (*)()>(DT_FINI_ARRAY);
        auto nr = dynamic_val(DT_FINI_ARRAYSZ) / sizeof(*funcs);
        // According to the standard, call functions in reverse order.
        for (int i = nr - 1; i >= 0; --i) {
            funcs[i]();
        }
    }
    // FIXME: may also need to run DT_FINI here (after DT_FINI_ARRAY), but
    // on modern gcc it seems it doesn't work (and isn't needed).
}

void* object::tls_addr()
{
    auto t = sched::thread::current();
    auto r = t->get_tls(_module_index);
    if (!r) {
        r = t->setup_tls(_module_index, _tls_segment, _tls_init_size, _tls_uninit_size);
    }
    return r;
}

program* s_program;

program::program(void* addr)
    : _next_alloc(addr)
{
    assert(!s_program);
    s_program = this;
    _core = make_shared<memory_image>(*this, reinterpret_cast<void*>(0x200000));
    assert(_core->module_index() == core_module_index);
    _core->load_segments();
    set_object("libc.so.6", _core);
    set_object("libm.so.6", _core);
    set_object("ld-linux-x86-64.so.2", _core);
    set_object("libpthread.so.0", _core);
    set_object("libdl.so.2", _core);
    set_object("librt.so.1", _core);
    set_object("libstdc++.so.6", _core);
    set_object("libboost_system-mt.so.1.53.0", _core);
    set_object("libboost_program_options-mt.so.1.53.0", _core);
}

void program::set_search_path(std::initializer_list<std::string> path)
{
    _search_path = path;
}

tls_data program::tls()
{
    return _core->tls();
}

void program::set_object(std::string name, std::shared_ptr<elf::object> obj)
{
    _files[name] = obj;
    if (std::find(_modules.begin(), _modules.end(), obj.get()) == _modules.end()) {
        _modules.push_back(obj.get());
        _modules_adds++;
    }
}

static std::string getcwd()
{
    auto r = ::getcwd(NULL, 0);
    std::string rs = r;
    free(r);
    return rs;
}

static std::string canonicalize(std::string p)
{
    auto r = realpath(p.c_str(), NULL);
    if (r) {
        std::string rs = r;
        free(r);
        return rs;
    } else {
        return p;
    }
}

std::shared_ptr<elf::object>
program::get_library(std::string name, std::vector<std::string> extra_path)
{
    fileref f;
    if (name.find('/') == name.npos) {
        std::vector<std::string> search_path;
        search_path.insert(search_path.end(), extra_path.begin(), extra_path.end());
        search_path.insert(search_path.end(), _search_path.begin(), _search_path.end());
        for (auto dir : search_path) {
            auto dname = canonicalize(dir + "/" + name);
            f = fileref_from_fname(dname);
            if (f) {
                name = dname;
                break;
            }
        }
    } else {
        if (name[0] != '/') {
            name = getcwd() + "/" + name;
        }
        name = canonicalize(name);
        f = fileref_from_fname(name);
    }

    if (_files.count(name)) {
        auto obj = _files[name].lock();
        assert(obj);
        return obj;
    } else if (f) {
        auto ef = std::make_shared<file>(*this, f, name);
        ef->set_base(_next_alloc);
        ef->setprivate(true);
        // We need to push the object at the end of the list (so that the main
        // shared object gets searched before the shared libraries it uses),
        // with one exception: the kernel needs to remain at the end of the
        // list - We want it to behave like a library, not the main program.
        _modules.insert(std::prev(_modules.end()), ef.get());
        _modules_adds++;
        ef->load_segments();
        _next_alloc = ef->end();
        add_debugger_obj(ef.get());
        ef->load_needed();
        ef->relocate();
        ef->run_init_funcs();
        ef->setprivate(false);
        _files[name] = ef;
        _files[ef->soname()] = ef;
        return ef;
    } else {
        return std::shared_ptr<object>();
    }
}

void program::remove_object(object *ef)
{
    ef->run_fini_funcs();
    ef->unload_needed();
    del_debugger_obj(ef);
    _files.erase(ef->pathname());
    _files.erase(ef->soname());
    _modules.erase(std::find(_modules.begin(), _modules.end(), ef));
    _modules_subs++;
    ef->unload_segments();
    // Note we don't delete(ef) here - the contrary, delete(ef) calls us.
}

object* program::s_objs[100];

void program::add_debugger_obj(object* obj)
{
    auto p = s_objs;
    while (*p) {
        ++p;
    }
    *p = obj;
}

void program::del_debugger_obj(object* obj)
{
    auto p = s_objs;
    while (*p && *p != obj) {
        ++p;
    }
    if (!*p) {
        return;
    }
    while ((p[0] = p[1]) != nullptr) {
        ++p;
    }
}

symbol_module program::lookup(const char* name)
{
    for (auto module : _modules) {
        if (auto sym = module->lookup_symbol(name)) {
            return symbol_module(sym, module);
        }
    }
    return symbol_module(nullptr, nullptr);
}

void* program::do_lookup_function(const char* name)
{
    auto sym = lookup(name);
    if (!sym.symbol) {
        throw std::runtime_error("symbol not found");
    }
    if ((sym.symbol->st_info & 15) != STT_FUNC) {
        throw std::runtime_error("symbol is not a function");
    }
    return sym.relocated_addr();
}

dladdr_info program::lookup_addr(const void* addr)
{
    for (auto module : _modules) {
        auto ret = module->lookup_addr(addr);
        if (ret.fname) {
            return ret;
        }
    }
    return {};
}

program* get_program()
{
    return s_program;
}

init_table get_init(Elf64_Ehdr* header)
{
    void* pbase = static_cast<void*>(header);
    void* base = pbase;
    auto phdr = static_cast<Elf64_Phdr*>(pbase + header->e_phoff);
    auto n = header->e_phnum;
    bool base_adjusted = false;
    init_table ret;
    for (auto i = 0; i < n; ++i, ++phdr) {
        if (!base_adjusted && phdr->p_type == PT_LOAD) {
            base_adjusted = true;
            base -= phdr->p_vaddr;
        }
        if (phdr->p_type == PT_DYNAMIC) {
            auto dyn = reinterpret_cast<Elf64_Dyn*>(phdr->p_vaddr);
            unsigned ndyn = phdr->p_memsz / sizeof(*dyn);
            const Elf64_Rela* rela = nullptr;
            const Elf64_Rela* jmp = nullptr;
            const Elf64_Sym* symtab = nullptr;
            const Elf64_Word* hashtab = nullptr;
            const char* strtab = nullptr;
            unsigned nrela = 0;
            unsigned njmp = 0;
            for (auto d = dyn; d < dyn + ndyn; ++d) {
                switch (d->d_tag) {
                case DT_INIT_ARRAY:
                    ret.start = reinterpret_cast<void (**)()>(d->d_un.d_ptr);
                    break;
                case DT_INIT_ARRAYSZ:
                    ret.count = d->d_un.d_val / sizeof(ret.start);
                    break;
                case DT_RELA:
                    rela = reinterpret_cast<const Elf64_Rela*>(d->d_un.d_ptr);
                    break;
                case DT_RELASZ:
                    nrela = d->d_un.d_val / sizeof(*rela);
                    break;
                case DT_SYMTAB:
                    symtab = reinterpret_cast<const Elf64_Sym*>(d->d_un.d_ptr);
                    break;
                case DT_HASH:
                    hashtab = reinterpret_cast<const Elf64_Word*>(d->d_un.d_ptr);
                    break;
                case DT_STRTAB:
                    strtab = reinterpret_cast<const char*>(d->d_un.d_ptr);
                    break;
                case DT_JMPREL:
                    jmp = reinterpret_cast<const Elf64_Rela*>(d->d_un.d_ptr);
                    break;
                case DT_PLTRELSZ:
                    njmp = d->d_un.d_val / sizeof(*jmp);
                    break;
                }
            }
            auto nbucket = hashtab[0];
            auto buckets = hashtab + 2;
            auto chain = buckets + nbucket;
            auto relocate_table = [=](const Elf64_Rela *rtab, unsigned n) {
                if (!rtab) {
                    return;
                }
                for (auto r = rtab; r < rtab + n; ++r) {
                    auto info = r->r_info;
                    u32 sym = info >> 32;
                    u32 type = info & 0xffffffff;
                    void *addr = base + r->r_offset;
                    auto addend = r->r_addend;
                    auto lookup = [=]() {
                        auto name = strtab + symtab[sym].st_name;
                        for (auto ent = buckets[elf64_hash(name) % nbucket];
                                ent != STN_UNDEF;
                                ent = chain[ent]) {
                            auto &sym = symtab[ent];
                            if (strcmp(name, &strtab[sym.st_name]) == 0) {
                                return &sym;
                            }
                        }
                        abort();
                    };
                    switch (type) {
                    case R_X86_64_NONE:
                        break;
                    case R_X86_64_64:
                        *static_cast<u64*>(addr) = lookup()->st_value + addend;
                        break;
                    case R_X86_64_RELATIVE:
                        *static_cast<void**>(addr) = base + addend;
                        break;
                    case R_X86_64_JUMP_SLOT:
                    case R_X86_64_GLOB_DAT:
                        *static_cast<u64*>(addr) = lookup()->st_value;
                        break;
                    case R_X86_64_DPTMOD64:
                        abort();
                        //*static_cast<u64*>(addr) = symbol_module(sym);
                        break;
                    case R_X86_64_DTPOFF64:
                        *static_cast<u64*>(addr) = lookup()->st_value;
                        break;
                    case R_X86_64_TPOFF64:
                        // FIXME: assumes TLS segment comes before DYNAMIC segment
                        *static_cast<u64*>(addr) = lookup()->st_value - ret.tls.size;
                        break;
                    case R_X86_64_IRELATIVE:
                        *static_cast<void**>(addr) = reinterpret_cast<void *(*)()>(base + addend)();
                        break;
                    default:
                        abort();
                    }

                }
            };
            relocate_table(rela, nrela);
            relocate_table(jmp, njmp);
        } else if (phdr->p_type == PT_TLS) {
            ret.tls.start = reinterpret_cast<void*>(phdr->p_vaddr);
            ret.tls.filesize = phdr->p_filesz;
            ret.tls.size = phdr->p_memsz;
        }
    }
    return ret;
}

ulong program::register_dtv(object* obj)
{
    auto i = find(_module_index_list, nullptr);
    if (i != _module_index_list.end()) {
        *i = obj;
        return i - _module_index_list.begin();
    } else {
        _module_index_list.push_back(obj);
        return _module_index_list.size() - 1;
    }
}

void program::free_dtv(object* obj)
{
    auto i = find(_module_index_list, obj);
    assert(i != _module_index_list.end());
    *i = nullptr;
}

void* program::tls_addr(ulong module)
{
    return _module_index_list[module]->tls_addr();
}

}

using namespace elf;

extern "C" { void* elf_resolve_pltgot(unsigned long index, elf::object* obj); }

void* elf_resolve_pltgot(unsigned long index, elf::object* obj)
{
    return obj->resolve_pltgot(index);
}

struct module_and_offset {
    ulong module;
    ulong offset;
};

extern "C"
void* __tls_get_addr(module_and_offset* mao)
{
    return s_program->tls_addr(mao->module) + mao->offset;
}
