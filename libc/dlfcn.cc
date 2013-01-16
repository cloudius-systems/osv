#include <dlfcn.h>
#include "elf.hh"
#include <link.h>

void* dlopen(const char* filename, int flags)
{
    auto prog = elf::get_program();
    elf::elf_object* obj = prog->add_object(filename);
    // FIXME: handle flags etc.
    return obj;
}

void* dlsym(void* handle, const char* name)
{
    // FIXME: don't ignore handle
    auto sym = elf::get_program()->lookup(name);
    if (!sym.object) {
        return nullptr;
    }
    return sym.relocated_addr();
}

int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *info,
                                    size_t size, void *data),
                    void *data)
{
    int ret = 0;
    elf::get_program()->with_modules([=, &ret] (std::vector<elf::elf_object*>& m) {
        for (auto obj : m) {
            dl_phdr_info info;
            info.dlpi_addr = reinterpret_cast<uintptr_t>(obj->base());
            std::string name = obj->pathname();
            info.dlpi_name = name.c_str();
            auto phdrs = obj->phdrs();
            // hopefully, the libc and osv types match:
            info.dlpi_phdr = reinterpret_cast<Elf64_Phdr*>(&*phdrs.begin());
            info.dlpi_phnum = phdrs.size();
            ret = callback(&info, sizeof(info), data);
            if (ret) {
                break;
            }
        }
    });
    return ret;
}
