
#include "drivers/isa-serial.hh"
#include "fs/bootfs.hh"
#include <boost/format.hpp>
#include <cctype>
#include "elf.hh"
#include "exceptions.hh"
#include "debug.hh"
#include "drivers/pci.hh"
#include <jni.h>
//#include <locale>

namespace {

    void test_locale()
    {
	auto loc = std::locale();
	auto &fac = std::use_facet<std::ctype<char>>(loc);
	bool ok = fac.is(std::ctype_base::digit, '3')
	    && !fac.is(std::ctype_base::digit, 'x');
	debug(ok ? "locale works" : "locale fails");
	//asm volatile ("1: jmp 1b");
    }

}

elf::Elf64_Ehdr* elf_header;

extern "C" { void premain(); }

void premain()
{
    auto inittab = elf::get_init(elf_header);
    for (auto init = inittab.start; init < inittab.start + inittab.count; ++init) {
        (*init)();
    }
}

int main(int ac, char **av)
{
    IsaSerialConsole console;

    Debug::Instance()->setConsole(&console);
    debug("Loader Copyright 2013 Unnamed");

    test_locale();
    interrupt_descriptor_table idt;
    idt.load_on_cpu();

    bootfs fs;
    rootfs = &fs;
    file* f = fs.open("/usr/lib/libjvm.so");
    char buf[100];
    f->read(buf, 0, 100);

#if 1
    if (std::isdigit('1'))
	debug("isgidit(1) = ok");
    else
	debug("isgidit(1) = bad");
    if (!std::isdigit('x'))
	debug("isgidit(x) = ok");
    else
	debug("isgidit(x) = bad");
#if 0
    auto &fac = std::use_facet<std::ctype<char> >(std::locale("C"));
    if (fac.is(std::ctype<char>::digit, '1'))
	debug("facet works");
    else
	debug("facet !works");
#endif
    //while (true)
    //	;
#endif

    debug(fmt("jvm: %1% bytes, contents %2% ") % f->size() % buf);
    elf::program prog(fs);
    prog.add("libjvm.so");
    auto JNI_GetDefaultJavaVMInitArgs
        = prog.lookup_function<void (void*)>("JNI_GetDefaultJavaVMInitArgs");
    JavaVMInitArgs vm_args;
    vm_args.version = JNI_VERSION_1_6;
    JNI_GetDefaultJavaVMInitArgs(&vm_args);
    auto JNI_CreateJavaVM
        = prog.lookup_function<jint (JavaVM**, void**, void*)>("JNI_CreateJavaVM");
    JavaVM* jvm = nullptr;

    pci::pci_devices_print();

    auto ret = JNI_CreateJavaVM(&jvm, nullptr, &vm_args);
    debug(fmt("JNI_CreateJavaVM() returned %1%") % ret);

    while (true)
	;
}
