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
    case R_X86_64_DPTMOD64:
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
    case R_X86_64_64:
        *static_cast<void**>(addr) = symbol(sym).relocated_addr() + addend;
        break;
    case R_X86_64_RELATIVE:
        *static_cast<void**>(addr) = _base + addend;
        break;
    case R_X86_64_JUMP_SLOT:
    case R_X86_64_GLOB_DAT:
        *static_cast<void**>(addr) = symbol(sym).relocated_addr();
        break;
    case R_X86_64_DPTMOD64:
        if (sym == STN_UNDEF) {
            *static_cast<u64*>(addr) = _module_index;
        } else {
            *static_cast<u64*>(addr) = symbol(sym).obj->_module_index;
        }
        break;
    case R_X86_64_DTPOFF64:
        *static_cast<u64*>(addr) = symbol(sym).symbol->st_value;
        break;
    case R_X86_64_TPOFF64:
        if (sym) {
            auto sm = symbol(sym);
            sm.obj->alloc_static_tls();
            auto tls_offset = sm.obj->static_tls_end() + sched::kernel_tls_size();
            *static_cast<u64*>(addr) = sm.symbol->st_value + addend - tls_offset;
        } else {
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

bool object::arch_relocate_jump_slot(u32 sym, void *addr, Elf64_Sxword addend)
{
    *static_cast<void**>(addr) = symbol(sym).relocated_addr();
    return true;
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
