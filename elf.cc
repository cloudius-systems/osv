#include "elf.hh"
#include "mmu.hh"
#include <boost/format.hpp>
#include <exception>
#include <memory>
#include <string.h>
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
            abort();
        }
    }

    elf_object::elf_object(program& prog)
        : _prog(prog)
        , _tls_segment()
        , _tls_init_size()
        , _tls_uninit_size()
        , _dynamic_table(nullptr)
    {
    }

    elf_object::~elf_object()
    {
    }

    elf_file::elf_file(program& prog, ::file* f)
	: elf_object(prog)
        , _f(*f)
    {
	load_elf_header();
	load_program_headers();
    }

    elf_file::~elf_file()
    {
        delete &_f;
    }

    elf_memory_image::elf_memory_image(program& prog, void* base)
        : elf_object(prog)
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

    void elf_file::load_elf_header()
    {
	_f.read(&_ehdr, 0, sizeof(_ehdr));
	debug(fmt("elf header: %1%") % _ehdr.e_ident);
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
	debug("loaded elf header");
    }

    namespace {

    void* align(void* addr, ulong align, ulong offset)
    {
        ulong a = reinterpret_cast<ulong>(addr);
        a -= offset;
        a = (a + align - 1) & ~(align - 1);
        a += offset;
        return reinterpret_cast<void*>(a);
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
        debug(fmt("base %p end %p") % _base % _end);
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
	debug(fmt("program headers: %1%") % _ehdr.e_phnum);
	_phdrs.resize(_ehdr.e_phnum);
	for (unsigned i = 0; i < _ehdr.e_phnum; ++i) {
	    _f.read(&_phdrs[i],
		    _ehdr.e_phoff + i * _ehdr.e_phentsize,
		    _ehdr.e_phentsize);
	    debug(fmt("phdr %1%: vaddr %2$16x")
				   % i % _phdrs[i].p_vaddr);
	}
    }

    namespace {

    ulong page_size = 4096;

    ulong align_down(ulong v)
    {
        return v & ~(page_size - 1);
    }

    ulong align_up(ulong v)
    {
        return align_down(v + page_size - 1);
    }

    }

    void elf_file::load_segment(const Elf64_Phdr& phdr)
    {
        ulong vstart = align_down(phdr.p_vaddr);
        ulong filesz = align_up(phdr.p_vaddr + phdr.p_filesz) - vstart;
        ulong memsz = align_up(phdr.p_vaddr + phdr.p_memsz) - vstart;
        mmu::map_file(_base + vstart, filesz, mmu::perm_rwx,
                      _f, align_down(phdr.p_offset));
        mmu::map_anon(_base + vstart + filesz, memsz - filesz, mmu::perm_rwx);
    }

    void elf_object::load_segments()
    {
        for (unsigned i = 0; i < _ehdr.e_phnum; ++i) {
            debug(fmt("loading segment %1%") % i);
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
                break;
            case PT_TLS:
                _tls_segment = _base + phdr.p_vaddr;
                _tls_init_size = phdr.p_filesz;
                _tls_uninit_size = phdr.p_memsz - phdr.p_filesz;
                break;
            case PT_GNU_EH_FRAME:
                load_segment(phdr);
                break;
            default:
                abort();
                throw std::runtime_error("bad p_type");
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
                *static_cast<u64*>(addr) = symbol(sym).symbol->st_value + addend;
                break;
            case R_X86_64_RELATIVE:
                *static_cast<void**>(addr) = _base + addend;
                break;
            case R_X86_64_JUMP_SLOT:
            case R_X86_64_GLOB_DAT:
                *static_cast<u64*>(addr) = symbol(sym).symbol->st_value;
                break;
            case R_X86_64_DPTMOD64:
                *static_cast<u64*>(addr) = symbol_tls_module(sym);
                break;
            case R_X86_64_DTPOFF64:
                *static_cast<u64*>(addr) = symbol(sym).symbol->st_value;
                break;
            case R_X86_64_TPOFF64:
                *static_cast<u64*>(addr) = symbol(sym).symbol->st_value;
                break;
            default:
                abort();
            }
        }
    }

    void elf_object::relocate()
    {
        assert(!dynamic_exists(DT_REL));
        if (dynamic_exists(DT_RELA)) {
            relocate_rela();
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
        if (dynamic_exists(DT_GNU_HASH)) {
            return lookup_symbol_gnu(name);
        } else {
            return lookup_symbol_old(name);
        }
    }

    void elf_object::load_needed()
    {
        auto needed = dynamic_str_array(DT_NEEDED);
        for (auto lib : needed) {
            debug(fmt("needed: %1%") % lib);
            _prog.add(lib);
        }
    }

    program::program(::filesystem& fs, void* addr)
        : _fs(fs)
        , _next_alloc(addr)
        , _core(new elf::elf_memory_image(*this, reinterpret_cast<void*>(0x200000)))
    {
        _core->load_segments();
        add("libc.so.6", _core.get());
        add("ld-linux-x86-64.so.2", _core.get());
        add("libpthread.so.0", _core.get());
        add("libdl.so.2", _core.get());
    }

    void program::add(std::string name, elf_object* obj)
    {
        _files[name] = obj;
    }

    void program::add(std::string name)
    {
        if (!_files.count(name)) {
            std::unique_ptr< ::file> f(_fs.open("/usr/lib/" + name));
            auto ef = new elf_file(*this, f.release());
            ef->set_base(_next_alloc);
            _files[name] = ef;
            ef->load_segments();
            _next_alloc = ef->end();
            ef->load_needed();
            ef->relocate();
        }
    }

    symbol_module program::lookup(const char* name)
    {
        // FIXME: correct lookup order?
        for (auto name_module : _files) {
            if (auto sym = name_module.second->lookup_symbol(name)) {
                return symbol_module(sym, name_module.second);
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

    init_table get_init(Elf64_Ehdr* header)
    {
        void* pbase = static_cast<void*>(header);
        void* base = pbase;
        auto phdr = static_cast<Elf64_Phdr*>(pbase + header->e_phoff);
        auto n = header->e_phnum;
        bool base_adjusted = false;
        for (auto i = 0; i < n; ++i, ++phdr) {
            if (!base_adjusted && phdr->p_type == PT_LOAD) {
                base_adjusted = true;
                base -= phdr->p_vaddr;
            }
            if (phdr->p_type == PT_DYNAMIC) {
                auto dyn = reinterpret_cast<Elf64_Dyn*>(phdr->p_vaddr);
                unsigned ndyn = phdr->p_memsz / sizeof(*dyn);
                init_table ret;
                const Elf64_Rela* rela;
                const Elf64_Rela* jmp;
                const Elf64_Sym* symtab;
                const Elf64_Word* hashtab;
                const char* strtab;
                unsigned nrela;
                unsigned njmp;
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
                            *static_cast<u64*>(addr) = lookup()->st_value;
                            break;
                        default:
                            abort();
                        }

                    }
                };
                relocate_table(rela, nrela);
                relocate_table(jmp, njmp);
                return ret;
            }
        }
        abort();
    }

}

