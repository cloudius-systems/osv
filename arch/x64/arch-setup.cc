#include "arch-setup.hh"
#include "mempool.hh"
#include "mmu.hh"
#include "types.hh"
#include <alloca.h>
#include <string.h>

struct multiboot_info_type {
    u32 flags;
    u32 mem_lower;
    u32 mem_upper;
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;
    u32 syms[4];
    u32 mmap_length;
    u32 mmap_addr;
    u32 drives_length;
    u32 drives_addr;
    u32 config_table;
    u32 boot_loader_name;
    u32 apm_table;
    u32 vbe_control_info;
    u32 vbe_mode_info;
    u16 vbe_mode;
    u16 vbe_interface_seg;
    u16 vbe_interface_off;
    u16 vbe_interface_len;
} __attribute__((packed));

struct e820ent {
    u32 ent_size;
    u64 addr;
    u64 size;
    u32 type;
} __attribute__((packed));

multiboot_info_type* multiboot_info;

void arch_setup_free_memory()
{
    // copy to stack so we don't free it now
    auto mb = *multiboot_info;
    auto tmp = alloca(mb.mmap_length);
    memcpy(tmp, reinterpret_cast<void*>(mb.mmap_addr), mb.mmap_length);
    auto p = tmp;
    ulong edata;
    asm ("lea .edata, %0" : "=rm"(edata));
    while (p < tmp + mb.mmap_length) {
        auto ent = static_cast<e820ent*>(p);
        if (ent->type == 1) {
            memory::phys_mem_size += ent->size;
            if (ent->addr < edata) {
                u64 adjust = std::min(edata - ent->addr, ent->size);
                ent->addr += adjust;
                ent->size -= adjust;
            }
            // FIXME: limit to mapped 1GB for now
            // later map all of memory and free it too
            u64 memtop = 1 << 30;
            if (ent->addr + ent->size >= memtop) {
                auto excess = ent->addr + ent->size - memtop;
                excess = std::min(ent->size, excess);
                ent->size -= excess;
            }
            mmu::free_initial_memory_range(ent->addr, ent->size);
        }
        p += ent->ent_size + 4;
    }
}
