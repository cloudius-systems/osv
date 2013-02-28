#include "elf.hh"
#include "mmu.hh"
#include <boost/format.hpp>
#include <exception>
#include <memory>
#include <string.h>
#include "align.hh"
#include "debug.hh"

namespace {
    typedef boost::format fmt;
}

namespace elf {

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
        , object()
    {
    }

    symbol_module::symbol_module(Elf64_Sym* _sym, elf_object* _obj)
        : symbol(_sym)
        , object(_obj)
    {
    }

    void* symbol_module::relocated_addr() const
    {
        void* base = object->base();
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
        default:
            debug(fmt("unknown relocation type %d") % symbol_type(*symbol));
            abort();
        }
    }

    elf_object::elf_object(program& prog, std::string pathname)
        : _prog(prog)
        , _pathname(pathname)
        , _tls_segment()
        , _tls_init_size()
        , _tls_uninit_size()
        , _dynamic_table(nullptr)
    {
    }

    elf_object::~elf_object()
    {
    }

    elf_file::elf_file(program& prog, ::fileref f, std::string pathname)
	: elf_object(prog, pathname)
        , _f(f)
    {
	load_elf_header();
	load_program_headers();
    }

    elf_file::~elf_file()
    {
    }

    elf_memory_image::elf_memory_image(program& prog, void* base)
        : elf_object(prog, "")
    {
        _ehdr = *static_cast<Elf64_Ehdr*>(base);
        auto p = static_cast<Elf64_Phdr*>(base + _ehdr.e_phoff);
        assert(_ehdr.e_phentsize == sizeof(*p));
        _phdrs.assign(p, p + _ehdr.e_phnum);
        set_base(base);
    }

    void elf_memory_image::load_segment(const Elf64_Phdr& phdr)
    {
    }
    
    void elf_memory_image::unload_segment(const Elf64_Phdr& phdr)
    {
    }

    void elf_file::load_elf_header()
    {
	_f->read(&_ehdr, 0, sizeof(_ehdr));
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

    void elf_object::set_base(void* base)
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

    void* elf_object::base() const
    {
        return _base;
    }

    void* elf_object::end() const
    {
        return _end;
    }

    void elf_file::load_program_headers()
    {
	_phdrs.resize(_ehdr.e_phnum);
	for (unsigned i = 0; i < _ehdr.e_phnum; ++i) {
	    _f->read(&_phdrs[i],
		    _ehdr.e_phoff + i * _ehdr.e_phentsize,
		    _ehdr.e_phentsize);
	}
    }

    namespace {

    ulong page_size = 4096;

    }

    void elf_file::load_segment(const Elf64_Phdr& phdr)
    {
        ulong vstart = align_down(phdr.p_vaddr, page_size);
        ulong filesz_unaligned = phdr.p_vaddr + phdr.p_filesz - vstart;
        ulong filesz = align_up(filesz_unaligned, page_size);
        ulong memsz = align_up(phdr.p_vaddr + phdr.p_memsz, page_size) - vstart;
        mmu::map_file(_base + vstart, filesz, mmu::perm_rwx,
                      *_f, align_down(phdr.p_offset, page_size));
        memset(_base + vstart + filesz_unaligned, 0, filesz - filesz_unaligned);
        mmu::map_anon(_base + vstart + filesz, memsz - filesz, mmu::perm_rwx);
    }

    void elf_object::load_segments()
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

    void elf_file::unload_segment(const Elf64_Phdr& phdr)
    {
        ulong vstart = align_down(phdr.p_vaddr, page_size);
        ulong filesz_unaligned = phdr.p_vaddr + phdr.p_filesz - vstart;
        ulong filesz = align_up(filesz_unaligned, page_size);
        ulong memsz = align_up(phdr.p_vaddr + phdr.p_memsz, page_size) - vstart;
        mmu::unmap(_base + vstart, filesz);
        mmu::unmap(_base + vstart + filesz, memsz - filesz);
    }

    void elf_object::unload_segments()
    {
        for (unsigned i = 0; i < _ehdr.e_phnum; ++i) {
            auto &phdr = _phdrs[i];
            switch (phdr.p_type) {
            case PT_LOAD:
                load_segment(phdr);
                break;
            default:
                break;
            }
         }
    }

    template <typename T>
    T* elf_object::dynamic_ptr(unsigned tag)
    {
        return static_cast<T*>(_base + dynamic_tag(tag).d_un.d_ptr);
    }

    Elf64_Xword elf_object::dynamic_val(unsigned tag)
    {
        return dynamic_tag(tag).d_un.d_val;
    }

    const char* elf_object::dynamic_str(unsigned tag)
    {
        return dynamic_ptr<const char>(DT_STRTAB) + dynamic_val(tag);
    }

    bool elf_object::dynamic_exists(unsigned tag)
    {
        return _dynamic_tag(tag);
    }

    Elf64_Dyn* elf_object::_dynamic_tag(unsigned tag)
    {
        for (auto p = _dynamic_table; p->d_tag != DT_NULL; ++p) {
            if (p->d_tag == tag) {
                return p;
            }
        }
        return nullptr;
    }

    Elf64_Dyn& elf_object::dynamic_tag(unsigned tag)
    {
        auto r = _dynamic_tag(tag);
        if (!r) {
            throw std::runtime_error("missing tag");
        }
        return *r;
    }

    std::vector<const char *>
    elf_object::dynamic_str_array(unsigned tag)
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

    symbol_module elf_object::symbol(unsigned idx)
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
            debug(fmt("failed looking up symbol %1%") % name);
            abort();
        }
        return ret;
    }

    Elf64_Xword elf_object::symbol_tls_module(unsigned idx)
    {
        debug("not looking up symbol module");
        return 0;
    }

    void elf_object::relocate_rela()
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
                *static_cast<u64*>(addr) = symbol_tls_module(sym);
                break;
            case R_X86_64_DTPOFF64:
                *static_cast<u64*>(addr) = symbol(sym).symbol->st_value;
                break;
            case R_X86_64_TPOFF64:
                *static_cast<u64*>(addr) = symbol(sym).symbol->st_value - tls_data().size;
                break;
            default:
                abort();
            }
        }
    }

    extern "C" { void __elf_resolve_pltgot(void); }

    void elf_object::relocate_pltgot()
    {
        auto rel = dynamic_ptr<Elf64_Rela>(DT_JMPREL);
        auto nrel = dynamic_val(DT_PLTRELSZ) / sizeof(*rel);
        for (auto p = rel; p < rel + nrel; ++p) {
            auto info = p->r_info;
              u32 type = info & 0xffffffff;
              assert(type = R_X86_64_JUMP_SLOT);
              void *addr = _base + p->r_offset;
              // The JUMP_SLOT entry already points back to the PLT, just
              // make sure it is relocated relative to the object base.
              *static_cast<u64*>(addr) += reinterpret_cast<u64>(_base);
        }
        auto pltgot = dynamic_ptr<void*>(DT_PLTGOT);
        // PLTGOT resolution has a special calling convention, with the symbol
        // index and some word pushed on the stack, so we need an assembly
        // stub to convert it back to the standard calling convention.
        pltgot[1] = this;
        pltgot[2] = reinterpret_cast<void*>(__elf_resolve_pltgot);
    }

    void* elf_object::resolve_pltgot(unsigned index)
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

    void elf_object::relocate()
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

    Elf64_Sym* elf_object::lookup_symbol_old(const char* name)
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

    Elf64_Sym* elf_object::lookup_symbol_gnu(const char* name)
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

    Elf64_Sym* elf_object::lookup_symbol(const char* name)
    {
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

    void elf_object::load_needed()
    {
        auto needed = dynamic_str_array(DT_NEEDED);
        for (auto lib : needed) {
            auto fullpath = std::string("/usr/lib/") + lib;
            if (_prog.add_object(fullpath) == nullptr)
                debug(fmt("could not load %s") % fullpath);
        }
    }

    tls_data elf_object::tls()
    {
        return tls_data{_tls_segment, _tls_init_size + _tls_uninit_size};
    }

    std::string elf_object::soname()
    {
        return dynamic_exists(DT_SONAME) ? dynamic_str(DT_SONAME) : std::string();
    }

    std::vector<Elf64_Phdr> elf_object::phdrs()
    {
        return _phdrs;
    }

    std::string elf_object::pathname()
    {
        return _pathname;
    }

    void elf_object::run_init_func()
    {
        if (!dynamic_exists(DT_INIT_ARRAY)) {
            return;
        }
        auto inits = dynamic_ptr<void (*)()>(DT_INIT_ARRAY);
        auto nr = dynamic_val(DT_INIT_ARRAYSZ) / sizeof(*inits);
        for (auto i = 0u; i < nr; ++i) {
            inits[i]();
        }
    }

    program* s_program;

    program::program(::filesystem& fs, void* addr)
        : _fs(fs)
        , _next_alloc(addr)
        , _core(new elf::elf_memory_image(*this, reinterpret_cast<void*>(0x200000)))
    {
        _core->load_segments();
        assert(!s_program);
        s_program = this;
        set_object("libc.so.6", _core.get());
        set_object("ld-linux-x86-64.so.2", _core.get());
        set_object("libpthread.so.0", _core.get());
        set_object("libdl.so.2", _core.get());
    }

    tls_data program::tls()
    {
        return _core->tls();
    }

    void program::set_object(std::string name, elf_object* obj)
    {
        _files[name] = obj;
        if (std::find(_modules.begin(), _modules.end(), obj) == _modules.end()) {
            _modules.push_back(obj);
        }
    }

    elf_object* program::add_object(std::string name)
    {
        if (!_files.count(name)) {
            auto f(_fs.open(name));
            if (!f) {
                return nullptr;
            }
            auto ef = new elf_file(*this, f, name);
            ef->set_base(_next_alloc);
            _files[name] = ef;
            _modules.push_back(ef);
            ef->load_segments();
            _next_alloc = ef->end();
            add_debugger_obj(ef);
            ef->load_needed();
            ef->relocate();
            ef->run_init_func();
        }

        // TODO: we'll need to refcount the objects here or in the dl*() wrappers
        return _files[name];
    }

    void program::remove_object(std::string name)
    {
        auto ef = _files[name];

        _files.erase(name);
        _modules.erase(std::find(_modules.begin(), _modules.end(), ef));
        ef->unload_segments();
        delete ef;
    }

    elf_object* program::s_objs[100];

    void program::add_debugger_obj(elf_object* obj)
    {
        auto p = s_objs;
        while (*p) {
            ++p;
        }
        *p = obj;
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

    void program::with_modules(std::function<void (std::vector<elf_object*>&)> f)
    {
        // FIXME: locking?
        std::vector<elf_object*> tmp = _modules;
        f(tmp);
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
                        default:
                            abort();
                        }

                    }
                };
                relocate_table(rela, nrela);
                relocate_table(jmp, njmp);
            } else if (phdr->p_type == PT_TLS) {
                ret.tls.start = reinterpret_cast<void*>(phdr->p_vaddr);
                ret.tls.size = phdr->p_memsz;
            }
        }
        return ret;
    }

}

extern "C" { void* elf_resolve_pltgot(unsigned long index, elf::elf_object* obj); }

void* elf_resolve_pltgot(unsigned long index, elf::elf_object* obj)
{
    return obj->resolve_pltgot(index);
}
