#include <dlfcn.h>
#include "elf.hh"

void* dlopen(const char* filename, int flags)
{
    auto prog = elf::get_program();
    elf::elf_object* obj = prog->add(filename);
    // FIXME: handle flags etc.
    return obj;
}


