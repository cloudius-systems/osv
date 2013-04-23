#include "arch-setup.hh"
#include "mempool.hh"
#include "mmu.hh"
#include "processor.hh"
#include "elf.hh"
#include <osv/types.h>
#include <alloca.h>
#include <string.h>

using namespace mmu;

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

extern char** __argv;
extern int __argc;

void parse_cmdline(multiboot_info_type& mb)
{
    auto p = reinterpret_cast<char*>(mb.cmdline);
    char* cmdline = strdup(p);
    static std::vector<char*> args;
    char* save;
    while ((p = strtok_r(cmdline, " \t\n", &save)) != nullptr) {
        args.push_back(p);
        cmdline = nullptr;
    }
    args.push_back(nullptr);
    __argv = args.data();
    __argc = args.size() - 1;
}

void setup_temporary_phys_map()
{
    // duplicate 1:1 mapping into phys_mem
    u64 cr3 = processor::read_cr3();
    auto pt = reinterpret_cast<u64*>(cr3);
    pt[pt_index(phys_mem, 3)] = pt[0];
}

void for_each_e820_entry(void* e820_buffer, unsigned size, void (*f)(e820ent e))
{
    auto p = e820_buffer;
    while (p < e820_buffer + size) {
        auto ent = static_cast<e820ent*>(p);
        if (ent->type == 1) {
            f(*ent);
        }
        p += ent->ent_size + 4;
    }
}

bool intersects(const e820ent& ent, u64 a)
{
    return a > ent.addr && a < ent.addr + ent.size;
}

e820ent truncate_below(e820ent ent, u64 a)
{
    u64 delta = a - ent.addr;
    ent.addr += delta;
    ent.size -= delta;
    return ent;
}

e820ent truncate_above(e820ent ent, u64 a)
{
    u64 delta = ent.addr + ent.size - a;
    ent.size -= delta;
    return ent;
}

extern elf::Elf64_Ehdr* elf_header;

void arch_setup_free_memory()
{
    static ulong edata;
    asm ("movl $.edata, %0" : "=rm"(edata));
    // copy to stack so we don't free it now
    auto mb = *multiboot_info;
    auto e820_buffer = alloca(mb.mmap_length);
    auto e820_size = mb.mmap_length;
    memcpy(e820_buffer, reinterpret_cast<void*>(mb.mmap_addr), e820_size);
    for_each_e820_entry(e820_buffer, e820_size, [] (e820ent ent) {
        memory::phys_mem_size += ent.size;
    });
    constexpr u64 initial_map = 1 << 30; // 1GB mapped by startup code
    setup_temporary_phys_map();

    // setup all memory up to 1GB.  We can't free any more, because no
    // page tables have been set up, so we can't reference the memory being
    // freed.
    for_each_e820_entry(e820_buffer, e820_size, [] (e820ent ent) {
        // can't free anything below edata, it's core code.
        // FIXME: can free below 2MB.
        if (ent.addr + ent.size <= edata) {
            return;
        }
        if (intersects(ent, edata)) {
            ent = truncate_below(ent, edata);
        }
        // ignore anything above 1GB, we haven't mapped it yet
        if (intersects(ent, initial_map)) {
            ent = truncate_above(ent, initial_map);
        } else if (ent.addr >= initial_map) {
            return;
        }
        mmu::free_initial_memory_range(ent.addr, ent.size);
    });
    mmu::linear_map(mmu::phys_mem, 0, initial_map, initial_map);
    // map the core, loaded 1:1 by the boot loader
    mmu::phys elf_phys = reinterpret_cast<mmu::phys>(elf_header);
    mmu::linear_map(elf_header, elf_phys, edata - elf_phys, 0x200000);
    // get rid of the command line, before low memory is unmapped
    parse_cmdline(mb);
    // now that we have some free memory, we can start mapping the rest
    mmu::switch_to_runtime_page_table();
    for_each_e820_entry(e820_buffer, e820_size, [] (e820ent ent) {
        // Ignore memory already freed above
        if (ent.addr + ent.size <= initial_map) {
            return;
        }
        if (intersects(ent, initial_map)) {
            ent = truncate_below(ent, initial_map);
        }
        mmu::linear_map(mmu::phys_mem + ent.addr, ent.addr, ent.size, ~0);
        mmu::free_initial_memory_range(ent.addr, ent.size);
    });
}
