#include <dlfcn.h>
#include "elf.hh"
#include <link.h>
#include <osv/debug.h>

void* dlopen(const char* filename, int flags)
{
    if (!filename) {
        // It is strange that we are returning a program while
        // dlsym will always try to open an object. We may have to
        // revisit this later, specially if this affect the ordering
        // semantics of lookups. But for now this will work
        return elf::get_program();
    }

    auto prog = elf::get_program();
    elf::object* obj = prog->add_object(filename);
    // FIXME: handle flags etc.
    return obj;
}

int dlclose(void* handle)
{
    debug("stub dlclose()\n");
    return 0;
}

void* dlsym(void* handle, const char* name)
{
    elf::symbol_module sym;
    auto program = elf::get_program();
    if ((program == handle) || (handle == RTLD_DEFAULT)) {
        sym = program->lookup(name);
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

extern "C"
int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *info,
                                    size_t size, void *data),
                    void *data)
{
    int ret = 0;
    elf::get_program()->with_modules([=, &ret] (const std::vector<elf::object*>& m) {
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

extern "C" int dladdr(void *addr, Dl_info *info)
{
    auto ei = elf::get_program()->lookup_addr(addr);
    info->dli_fname = ei.fname;
    info->dli_fbase = ei.base;
    info->dli_sname = ei.sym;
    info->dli_saddr = ei.addr;
    return 0;
}
