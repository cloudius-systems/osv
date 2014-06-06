/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/elf.hh>

namespace elf {

bool object::arch_relocate_rela(u32 type, u32 sym, void *addr,
                                Elf64_Sxword addend)
{
    switch (type) {
    case R_X86_64_NONE:
        break;
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
        *static_cast<u64*>(addr) = symbol(sym).symbol->st_value - get_tls_size();
        break;
    default:
        return false;
    }

    return true;
}

}
