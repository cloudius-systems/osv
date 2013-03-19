#include <dlfcn.h>
#include "elf.hh"
#include <link.h>
#include <osv/debug.h>

void* dlopen(const char* filename, int flags)
{
    auto prog = elf::get_program();
    elf::object* obj = prog->add_object(filename);
    // FIXME: handle flags etc.
    return obj;
}

void* dlsym(void* handle, const char* name)
{
    elf::symbol_module sym;
    if (handle == RTLD_DEFAULT) {
        sym = elf::get_program()->lookup(name);
    } else if (handle == RTLD_NEXT) {
        // FIXME: implement
        abort();
    } else {
        auto obj = reinterpret_cast<elf::object*>(handle);
        sym = { obj->lookup_symbol(name), obj };
    }
    if (!sym.obj || !sym.symbol) {
        return nullptr;
    }
    return sym.relocated_addr();
}

// osv-local function to dlclose by path, as we can't remove objects by handle yet.
extern "C" void dlclose_by_path_np(const char* filename)
{
    elf::get_program()->remove_object(filename);
}

int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *info,
                                    size_t size, void *data),
                    void *data)
{
    int ret = 0;
    elf::get_program()->with_modules([=, &ret] (std::vector<elf::object*>& m) {
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

extern "C" int dladdr(__const void *addr, Dl_info *info)
{
    kprintf("stub dladdr()\n");
    errno = EINVAL;
    return -1;
}
