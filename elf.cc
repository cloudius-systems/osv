#include "elf.hh"
#include "drivers/console.hh"
#include "mmu.hh"
#include <boost/format.hpp>
#include <exception>
#include <memory>

extern Console *debug_console;

namespace {
    typedef boost::format fmt;
}

namespace elf {

    elf_object::elf_object(program& prog)
        : _prog(prog)
        , _tls_segment()
        , _tls_init_size()
        , _tls_uninit_size()
        , _dynamic_table(nullptr)
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

    void elf_file::load_elf_header()
    {
	_f.read(&_ehdr, 0, sizeof(_ehdr));
	debug_console->writeln(fmt("elf header: %1%") % _ehdr.e_ident);
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
	debug_console->writeln("loaded elf header");
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
        debug_console->writeln(fmt("base %p end %p") % _base % _end);
    }

    void* elf_object::end() const
    {
        return _end;
    }

    void elf_file::load_program_headers()
    {
	debug_console->writeln(fmt("program headers: %1%") % _ehdr.e_phnum);
	_phdrs.resize(_ehdr.e_phnum);
	for (unsigned i = 0; i < _ehdr.e_phnum; ++i) {
	    _f.read(&_phdrs[i],
		    _ehdr.e_phoff + i * _ehdr.e_phentsize,
		    _ehdr.e_phentsize);
	    debug_console->writeln(fmt("phdr %1%: vaddr %2$16x")
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

    void elf_file::load_segments()
    {
        for (unsigned i = 0; i < _ehdr.e_phnum; ++i) {
            debug_console->writeln(fmt("loading segment %1%") % i);
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
        return static_cast<T*>(_base + lookup(tag).d_un.d_ptr);
    }

    Elf64_Xword elf_object::dynamic_val(unsigned tag)
    {
        return lookup(tag).d_un.d_val;
    }

    const char* elf_object::dynamic_str(unsigned tag)
    {
        return dynamic_ptr<const char>(DT_STRTAB) + dynamic_val(tag);
    }

    bool elf_object::dynamic_exists(unsigned tag)
    {
        return _lookup(tag);
    }

    Elf64_Dyn* elf_object::_lookup(unsigned tag)
    {
        for (auto p = _dynamic_table; p->d_tag != DT_NULL; ++p) {
            if (p->d_tag == tag) {
                return p;
            }
        }
        return nullptr;
    }

    Elf64_Dyn& elf_object::lookup(unsigned tag)
    {
        auto r = _lookup(tag);
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

    Elf64_Xword elf_object::symbol(unsigned idx)
    {
        auto symtab = dynamic_ptr<Elf64_Sym>(DT_SYMTAB);
        assert(dynamic_val(DT_SYMENT) == sizeof(Elf64_Sym));
        auto nameidx = symtab[idx].st_name;
        auto name = dynamic_ptr<const char>(DT_STRTAB) + nameidx;
        debug_console->writeln(fmt("not looking up %1%(%2%)") % name % idx);
        return 0;
    }

    Elf64_Xword elf_object::symbol_module(unsigned idx)
    {
        debug_console->writeln("not looking up symbol module");
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
                *static_cast<u64*>(addr) = symbol(sym) + addend;
                break;
            case R_X86_64_RELATIVE:
                *static_cast<void**>(addr) = _base + addend;
                break;
            case R_X86_64_JUMP_SLOT:
            case R_X86_64_GLOB_DAT:
                *static_cast<u64*>(addr) = symbol(sym);
                break;
            case R_X86_64_DPTMOD64:
                *static_cast<u64*>(addr) = symbol_module(sym);
                break;
            case R_X86_64_TPOFF64:
                *static_cast<u64*>(addr) = symbol(sym);
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

    void elf_object::load_needed()
    {
        auto needed = dynamic_str_array(DT_NEEDED);
        for (auto lib : needed) {
            debug_console->writeln(fmt("needed: %1%") % lib);
            _prog.add(lib);
        }
    }

    program::program(::filesystem& fs, void* addr)
        : _fs(fs)
        , _next_alloc(addr)
    {
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
}

void load_elf(std::string name, ::filesystem& fs, void* addr)
{
    elf::program prog(fs, addr);
    // load the kernel statically as libc.so.6, since it provides the C library
    // API to other objects.  see loader.ld for the base address.
    auto core = new elf::elf_memory_image(prog, reinterpret_cast<void*>(0x200000));
    prog.add("libc.so.6", core);
    prog.add("ld-linux-x86-64.so.2", core);
    prog.add(name);
    abort();
}

