/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <dlfcn.h>
#include <osv/elf.hh>
#include <link.h>
#include <debug.hh>

static __thread char dlerror_msg[128];
static __thread char *dlerror_ptr;

static char *dlerror_set(char *val)
{
    char *old = dlerror_ptr;

    dlerror_ptr = val;

    return old;
}

static void dlerror_fmt(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsnprintf(dlerror_msg, sizeof(dlerror_msg), fmt, args);
    va_end(args);

    dlerror_set(dlerror_msg);
}

void* dlopen(const char* filename, int flags)
{
    if (!filename || !strcmp(filename, "")) {
        // It is strange that we are returning a program while
        // dlsym will always try to open an object. We may have to
        // revisit this later, specially if this affect the ordering
        // semantics of lookups. But for now this will work
        return elf::get_program();
    }

    std::shared_ptr<elf::object> obj =
            elf::get_program()->get_library(filename);
    // FIXME: handle flags etc.
    if (!obj) {
        dlerror_fmt("dlopen: failed to open %s", filename);
        return nullptr;
    }
    return new std::shared_ptr<elf::object> (std::move(obj));
}

int dlclose(void* handle)
{
    // FIXME: before we allow dlclose(), we need to keep for each object a set
    // of shared_ptr to other objects which were used to resolve its symbols,
    // so that we don't unmap an object while its symbols are being used by
    // another.
    // delete ((std::shared_ptr<elf::object>*) handle);
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
        auto obj = *reinterpret_cast<std::shared_ptr<elf::object>*>(handle);
        sym = { obj->lookup_symbol(name), obj.get() };
    }
    if (!sym.obj || !sym.symbol) {
        dlerror_fmt("dlsym: symbol %s not found", name);
        return nullptr;
    }
    return sym.relocated_addr();
}

extern "C"
int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *info,
                                    size_t size, void *data),
                    void *data)
{
    int ret = 0;
    elf::get_program()->with_modules([=, &ret] (
            const std::vector<elf::object*>& m, int adds, int subs) {
        for (auto obj : m) {
            dl_phdr_info info;
            info.dlpi_addr = reinterpret_cast<uintptr_t>(obj->base());
            std::string name = obj->pathname();
            info.dlpi_name = name.c_str();
            auto phdrs_vectorp = obj->phdrs();
            // Note that the callback may (as long as adds/subs don't change)
            // keep around pointers to phdrs_array, so we assume obj->phdrs()
            // doesn't move around or change.
            auto phdrs_array = &((*phdrs_vectorp)[0]);
            auto phdrs_size = phdrs_vectorp->size();
            // hopefully, the libc and osv types match:
            info.dlpi_phdr = reinterpret_cast<const Elf64_Phdr*>(phdrs_array);
            info.dlpi_phnum = phdrs_size;
            info.dlpi_adds = adds;
            info.dlpi_subs = subs;
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
    /* dladdr returns non-zero on success and 0 on error */
    return 1;
}

extern "C" char *dlerror(void)
{
    return dlerror_set(nullptr);
}
