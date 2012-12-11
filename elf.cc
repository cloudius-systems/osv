#include "elf.hh"
#include "drivers/console.hh"
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
}

void load_elf(file& f, unsigned long addr)
{
    elf::elf_file ef(f);
}
