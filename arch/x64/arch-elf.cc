/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/elf.hh>
#include <osv/sched.hh>

namespace elf {

// This function is solely used to relocate symbols in OSv kernel ELF
// and is indirectly called by loader premain() function
bool arch_init_reloc_dyn(struct init_table *t, u32 type, u32 sym,
                         void *addr, void *base, Elf64_Sxword addend)
{
    switch (type) {
    case R_X86_64_NONE:
        break;
    case R_X86_64_COPY: {
        const Elf64_Sym *st = t->dyn_tabs.lookup(sym);
        memcpy(addr, (void *)st->st_value, st->st_size);
        break;
    }
    case R_X86_64_64:
        *static_cast<u64*>(addr) = t->dyn_tabs.lookup(sym)->st_value + addend;
        break;
    case R_X86_64_RELATIVE:
        *static_cast<void**>(addr) = base + addend;
        break;
    case R_X86_64_JUMP_SLOT:
    case R_X86_64_GLOB_DAT:
        *static_cast<u64*>(addr) = t->dyn_tabs.lookup(sym)->st_value;
        break;
    case R_X86_64_DTPMOD64:
        abort();
        //*static_cast<u64*>(addr) = symbol_module(sym);
        break;
    case R_X86_64_DTPOFF64:
        *static_cast<u64*>(addr) = t->dyn_tabs.lookup(sym)->st_value;
        break;
    case R_X86_64_TPOFF64:
        // FIXME: assumes TLS segment comes before DYNAMIC segment
        *static_cast<u64*>(addr) = t->dyn_tabs.lookup(sym)->st_value - t->tls.size;
        break;
    case R_X86_64_IRELATIVE:
        *static_cast<void**>(addr) = reinterpret_cast<void *(*)()>(base + addend)();
        break;
    default:
        return false;
    }
    return true;
}

//
// This method is used when relocating symbols in all ELF objects
// except for OSv kernel ELF itself which is relocated by
// the function arch_init_reloc_dyn() above
bool object::arch_relocate_rela(u32 type, u32 sym, void *addr,
                                Elf64_Sxword addend)
{
    switch (type) {
    case R_X86_64_NONE:
        break;
    case R_X86_64_COPY: {
        symbol_module sm = symbol_other(sym);
        memcpy(addr, sm.relocated_addr(), sm.size());
        break;
    }
    case R_X86_64_64: {
        auto _sym = symbol(sym, true);
        if (_sym.symbol) {
            *static_cast<void**>(addr) = _sym.relocated_addr() + addend;
        } else {
            *static_cast<void**>(addr) = missing_symbols_page_addr;
        }
        break;
    }
    case R_X86_64_RELATIVE:
        *static_cast<void**>(addr) = _base + addend;
        break;
    case R_X86_64_JUMP_SLOT:
    case R_X86_64_GLOB_DAT: {
        auto _sym = symbol(sym, true);
        if (_sym.symbol) {
            *static_cast<void**>(addr) = _sym.relocated_addr();
        } else {
            *static_cast<void**>(addr) = missing_symbols_page_addr;
        }
        break;
    }
    // The next 3 types are intended to relocate symbols of thread local variables
    // defined with __thread modifier
    //
    // Please note that thread local variables accessed in so called local-exec mode
    // are never relocated as their negative offsets relative to the TCB address in FS register,
    // are placed by static linker into the final code as in this example:
    //    mov %fs:0xfffffffffffffffc,%eax
    //
    case R_X86_64_DTPMOD64:
        // This type and next R_X86_64_DTPOFF64 are intended to prepare execution of __tls_get_addr()
        // which provides dynamic access of thread local variable
        // This calculates the module index of the ELF containing the variable
        if (sym == STN_UNDEF) {
            // The thread-local variable being accessed is within
            // the SAME shared object as the caller
            *static_cast<u64*>(addr) = _module_index;
            // No need to calculate the offset to the beginning
        } else {
            // The thread-local variable being accessed is located
            // in DIFFERENT shared object that the caller
            *static_cast<u64*>(addr) = symbol(sym).obj->_module_index;
        }
        break;
    case R_X86_64_DTPOFF64:
        // The thread-local variable being accessed is located
        // in DIFFERENT shared object that the caller
        *static_cast<u64*>(addr) = symbol(sym).symbol->st_value;
        break;
    case R_X86_64_TPOFF64:
        // This type is intended to resolve symbols of thread-local variables in static TLS
        // accessed in initial-exec mode and is handled to calculate the virtual address of
        // target thread-local variable
        if (sym) {
            auto sm = symbol(sym);
            sm.obj->alloc_static_tls();
            auto tls_offset = sm.obj->static_tls_end() + sched::kernel_tls_size();
            *static_cast<u64*>(addr) = sm.symbol->st_value + addend - tls_offset;
        } else {
            // TODO: Which case does this handle?
            alloc_static_tls();
            auto tls_offset = static_tls_end() + sched::kernel_tls_size();
            *static_cast<u64*>(addr) = addend - tls_offset;
        }
        break;
    default:
        return false;
    }

    return true;
}

bool object::arch_relocate_jump_slot(u32 sym, void *addr, Elf64_Sxword addend, bool ignore_missing)
{
    auto _sym = symbol(sym, ignore_missing);
    if (_sym.symbol) {
        *static_cast<void**>(addr) = _sym.relocated_addr();
         return true;
    } else {
         return false;
    }
}

void object::prepare_initial_tls(void* buffer, size_t size,
                                 std::vector<ptrdiff_t>& offsets)
{
    if (!_static_tls) {
        return;
    }
    auto tls_size = get_tls_size();
    auto ptr = static_cast<char*>(buffer) + size - _static_tls_offset - tls_size;
    memcpy(ptr, _tls_segment, _tls_init_size);
    memset(ptr + _tls_init_size, 0, _tls_uninit_size);

    offsets.resize(std::max(_module_index + 1, offsets.size()));
    offsets[_module_index] = - _static_tls_offset - tls_size - sched::kernel_tls_size();
}

}
