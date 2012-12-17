
#include "drivers/vga.hh"
#include "fs/bootfs.hh"
#include <boost/format.hpp>
#include <cctype>
#include "elf.hh"
#include "exceptions.hh"
//#include <locale>

typedef boost::format fmt;

extern void (*debug_write)(const char *msg);

Console *debug_console;

void console_debug_write(const char *msg)
{
    debug_console->writeln(msg);
}

namespace {

    void test_locale()
    {
	auto loc = std::locale();
	auto &fac = std::use_facet<std::ctype<char>>(loc);
	bool ok = fac.is(std::ctype_base::digit, '3')
	    && !fac.is(std::ctype_base::digit, 'x');
	debug_write(ok ? "locale works" : "locale fails");
	//asm volatile ("1: jmp 1b");
    }

}

extern void (*init_array_start[])(void), (*init_array_end[])(void);

int main(int ac, char **av)
{
    VGAConsole console;

    debug_console = &console;
    debug_write = console_debug_write;
    console.writeln("Loader Copyright 2013 Unnamed");

    for (auto init = init_array_start; init < init_array_end; ++init) {
	(*init)();
    }

    test_locale();
    interrupt_descriptor_table idt;
    idt.load_on_cpu();

    bootfs fs;
    file* f = fs.open("/usr/lib/libjvm.so");
    char buf[100];
    f->read(buf, 0, 100);

#if 1
    if (std::isdigit('1'))
	console.writeln("isgidit(1) = ok");
    else
	console.writeln("isgidit(1) = bad");
    if (!std::isdigit('x'))
	console.writeln("isgidit(x) = ok");
    else
	console.writeln("isgidit(x) = bad");
#if 0
    auto &fac = std::use_facet<std::ctype<char> >(std::locale("C"));
    if (fac.is(std::ctype<char>::digit, '1'))
	console.writeln("facet works");
    else
	console.writeln("facet !works");
#endif
    //while (true)
    //	;
#endif

    console.writeln(fmt("jvm: %1% bytes, contents %2% ") % f->size() % buf);
    load_elf("/usr/lib/libjvm.so", fs);
    while (true)
	;
}
