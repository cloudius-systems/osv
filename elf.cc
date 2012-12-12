#include "elf.hh"
#include "drivers/console.hh"
#include "mmu.hh"
#include <boost/format.hpp>
#include <exception>

extern Console *debug_console;

namespace {
    typedef boost::format fmt;
}

namespace elf {

    elf_file::elf_file(::file& f)
	: _f(f)
    {
	load_elf_header();
	load_program_headers();
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
	if (!(_ehdr.e_ident[EI_OSABI] == ELFOSABI_LINUX)) {
	    throw std::runtime_error("bad os abi");
	}
	debug_console->writeln("loaded elf header");
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

    void elf_file::load_segment(const Elf64_Phdr& phdr)
    {
        mmu::mmap(_f, phdr.p_offset, phdr.p_memsz, phdr.p_align,
                  phdr.p_vaddr & (phdr.p_align - 1), mmu::perm_rwx);
    }

    void elf_file::load_segments()
    {
        for (unsigned i = 0; i < _ehdr.e_phnum; ++i) {
            auto &phdr = _phdrs[i];
            switch (phdr.p_type) {
            case PT_NULL:
                break;
            case PT_LOAD:
                load_segment(phdr);
                break;
            default:
                throw std::runtime_error("bad p_type");
            }
        }
    }

}

void load_elf(file& f, unsigned long addr)
{
    elf::elf_file ef(f);
    // FIXME: don't ignore addr
    ef.load_segments();
}
