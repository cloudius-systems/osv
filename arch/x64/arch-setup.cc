/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "arch.hh"
#include "arch-cpu.hh"
#include "arch-setup.hh"
#include <osv/mempool.hh>
#include <osv/mmu.hh>
#include "processor.hh"
#include "processor-flags.h"
#include "msr.hh"
#include <osv/xen.hh>
#include <osv/elf.hh>
#include <osv/types.h>
#include <alloca.h>
#include <string.h>
#include <osv/boot.hh>
#include <osv/commands.hh>
#include "dmi.hh"

osv_multiboot_info_type* osv_multiboot_info;

#include "drivers/virtio-mmio.hh"
void parse_cmdline(multiboot_info_type& mb)
{
    auto p = reinterpret_cast<char*>(mb.cmdline);
    virtio::parse_mmio_device_configuration(p);
    osv::parse_cmdline(p);
}

void setup_temporary_phys_map()
{
    // duplicate 1:1 mapping into phys_mem
    u64 cr3 = processor::read_cr3();
    auto pt = reinterpret_cast<u64*>(cr3);
    for (auto&& area : mmu::identity_mapped_areas) {
        auto base = reinterpret_cast<void*>(get_mem_area_base(area));
        pt[mmu::pt_index(base, 3)] = pt[0];
    }
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
extern size_t elf_size;
extern void* elf_start;
extern boot_time_chart boot_time;

// Because vmlinux_entry64 replaces start32 as a new entry of loader.elf we need a way
// to place address of start32 so that boot16 know where to jump to. We achieve
// it by placing address of start32 at the known offset at memory
// as defined by section .start32_address in loader.ld
extern "C" void start32();
void * __attribute__((section (".start32_address"))) start32_address =
  reinterpret_cast<void*>((long)&start32 - OSV_KERNEL_VM_SHIFT);

extern "C" void start32_from_vmlinuz();
void * __attribute__((section (".start32_from_vmlinuz_address"))) start32_from_vmlinuz_address =
  reinterpret_cast<void*>((long)&start32_from_vmlinuz - OSV_KERNEL_VM_SHIFT);

void arch_setup_free_memory()
{
    static ulong edata, edata_phys;
    asm ("movl $.edata, %0" : "=rm"(edata));
    edata_phys = edata - OSV_KERNEL_VM_SHIFT;

    // copy to stack so we don't free it now
    auto omb = *osv_multiboot_info;
    auto mb = omb.mb;
    auto e820_buffer = alloca(mb.mmap_length);
    auto e820_size = mb.mmap_length;
    memcpy(e820_buffer, reinterpret_cast<void*>(mb.mmap_addr), e820_size);
    for_each_e820_entry(e820_buffer, e820_size, [] (e820ent ent) {
        memory::phys_mem_size += ent.size;
    });
    constexpr u64 initial_map = 1 << 30; // 1GB mapped by startup code

    u64 time;
    time = omb.tsc_init_hi;
    time = (time << 32) | omb.tsc_init;
    boot_time.event(0, "", time );

    time = omb.tsc_disk_done_hi;
    time = (time << 32) | omb.tsc_disk_done;
    boot_time.event(1, "disk read (real mode)", time );

    time = omb.tsc_uncompress_done_hi;
    time = (time << 32) | omb.tsc_uncompress_done;
    boot_time.event(2, "uncompress lzloader.elf", time );

    auto c = processor::cpuid(0x80000000);
    if (c.a >= 0x80000008) {
        c = processor::cpuid(0x80000008);
        mmu::phys_bits = c.a & 0xff;
        mmu::virt_bits = (c.a >> 8) & 0xff;
        assert(mmu::phys_bits <= mmu::max_phys_bits);
    }

    setup_temporary_phys_map();

    // setup all memory up to 1GB.  We can't free any more, because no
    // page tables have been set up, so we can't reference the memory being
    // freed.
    for_each_e820_entry(e820_buffer, e820_size, [] (e820ent ent) {
        // can't free anything below edata_phys, it's core code.
        // can't free anything below kernel at this moment
        if (ent.addr + ent.size <= edata_phys) {
            return;
        }
        if (intersects(ent, edata_phys)) {
            ent = truncate_below(ent, edata_phys);
        }
        // ignore anything above 1GB, we haven't mapped it yet
        if (intersects(ent, initial_map)) {
            ent = truncate_above(ent, initial_map);
        } else if (ent.addr >= initial_map) {
            return;
        }
        mmu::free_initial_memory_range(ent.addr, ent.size);
    });
    for (auto&& area : mmu::identity_mapped_areas) {
        auto base = reinterpret_cast<void*>(get_mem_area_base(area));
        mmu::linear_map(base, 0, initial_map, initial_map);
    }
    // Map the core, loaded by the boot loader
    // In order to properly setup mapping between virtual
    // and physical we need to take into account where kernel
    // is loaded in physical memory - elf_phys_start - and
    // where it is linked to start in virtual memory - elf_start
    static mmu::phys elf_phys_start = reinterpret_cast<mmu::phys>(elf_header);
    // There is simple invariant between elf_phys_start and elf_start
    // as expressed by the assignment below
    elf_start = reinterpret_cast<void*>(elf_phys_start + OSV_KERNEL_VM_SHIFT);
    elf_size = edata_phys - elf_phys_start;
    mmu::linear_map(elf_start, elf_phys_start, elf_size, OSV_KERNEL_BASE);
    // get rid of the command line, before low memory is unmapped
    parse_cmdline(mb);
    // now that we have some free memory, we can start mapping the rest
    mmu::switch_to_runtime_page_tables();
    for_each_e820_entry(e820_buffer, e820_size, [] (e820ent ent) {
        //
        // Free the memory below elf_phys_start which we could not before
        if (ent.addr < (u64)elf_phys_start) {
            if (ent.addr + ent.size >= (u64)elf_phys_start) {
                ent = truncate_above(ent, (u64) elf_phys_start);
            }
            mmu::free_initial_memory_range(ent.addr, ent.size);
            return;
        }
        //
        // Ignore memory already freed above
        if (ent.addr + ent.size <= initial_map) {
            return;
        }
        if (intersects(ent, initial_map)) {
            ent = truncate_below(ent, initial_map);
        }
        for (auto&& area : mmu::identity_mapped_areas) {
            auto base = reinterpret_cast<void*>(get_mem_area_base(area));
            mmu::linear_map(base + ent.addr, ent.addr, ent.size, ~0);
        }
        mmu::free_initial_memory_range(ent.addr, ent.size);
    });
}

void arch_setup_tls(void *tls, const elf::tls_data& info)
{
    struct thread_control_block *tcb;
    memcpy(tls, info.start, info.filesize);
    memset(tls + info.filesize, 0, info.size - info.filesize);
    tcb = (struct thread_control_block *)(tls + info.size);
    tcb->self = tcb;
    processor::wrmsr(msr::IA32_FS_BASE, reinterpret_cast<uint64_t>(tcb));
}

static inline void disable_pic()
{
    // PIC not present in Xen
    XENPV_ALTERNATIVE({ processor::outb(0xff, 0x21); processor::outb(0xff, 0xa1); }, {});
}

extern "C" void syscall_entry(void);

// SYSCALL Enable
static const int IA32_EFER_SCE = 0x1 << 0;
// Selector shift
static const int CS_SELECTOR_SHIFT = 3;
// syscall shift
static const int IA_32_STAR_SYSCALL_SHIFT = 32;

namespace processor {
void init_syscall() {
    unsigned long cs = gdt_cs;
    processor::wrmsr(msr::IA32_STAR,  (cs << CS_SELECTOR_SHIFT) << IA_32_STAR_SYSCALL_SHIFT);
    // lstar is where syscall set rip so we set it to syscall_entry
    processor::wrmsr(msr::IA32_LSTAR, reinterpret_cast<uint64_t>(syscall_entry));
    // syscall does rflag = rflag and not fmask
    // we want no minimize the impact of the syscall instruction so we choose
    // fmask as zero
    processor::wrmsr(msr::IA32_FMASK, 0);
    processor::wrmsr(msr::IA32_EFER,  processor::rdmsr(msr::IA32_EFER) | IA32_EFER_SCE);
}
}

void arch_init_premain()
{
    auto omb = *osv_multiboot_info;
    if (omb.disk_err)
	debug_early_u64("Error reading disk (real mode): ", static_cast<u64>(omb.disk_err));

    disable_pic();
}

#include "drivers/driver.hh"
#include "drivers/pvpanic.hh"
#include "drivers/virtio.hh"
#include "drivers/virtio-blk.hh"
#include "drivers/virtio-scsi.hh"
#include "drivers/virtio-net.hh"
#include "drivers/virtio-rng.hh"
#include "drivers/xenplatform-pci.hh"
#include "drivers/ahci.hh"
#include "drivers/vmw-pvscsi.hh"
#include "drivers/vmxnet3.hh"
#include "drivers/ide.hh"

extern bool opt_pci_disabled;
void arch_init_drivers()
{
    // initialize panic drivers
    panic::pvpanic::probe_and_setup();
    boot_time.event("pvpanic done");

    if (!opt_pci_disabled) {
        // Enumerate PCI devices
        pci::pci_device_enumeration();
        boot_time.event("pci enumerated");
    }

    // Register any parsed virtio-mmio devices
    virtio::register_mmio_devices(device_manager::instance());

    // Initialize all drivers
    hw::driver_manager* drvman = hw::driver_manager::instance();
    drvman->register_driver(virtio::blk::probe);
    drvman->register_driver(virtio::scsi::probe);
    drvman->register_driver(virtio::net::probe);
    drvman->register_driver(virtio::rng::probe);
    drvman->register_driver(xenfront::xenplatform_pci::probe);
    drvman->register_driver(ahci::hba::probe);
    drvman->register_driver(vmw::pvscsi::probe);
    drvman->register_driver(vmw::vmxnet3::probe);
    drvman->register_driver(ide::ide_drive::probe);
    boot_time.event("drivers probe");
    drvman->load_all();
    drvman->list_drivers();
}

#include "drivers/console.hh"
#include "drivers/isa-serial.hh"
#include "drivers/vga.hh"
#include "early-console.hh"

void arch_init_early_console()
{
    console::isa_serial_console::early_init();
}

bool arch_setup_console(std::string opt_console)
{
    hw::driver_manager* drvman = hw::driver_manager::instance();

    if (opt_console.compare("serial") == 0) {
        console::console_driver_add(&console::arch_early_console);
    } else if (opt_console.compare("vga") == 0) {
        drvman->register_driver(console::VGAConsole::probe);
    } else if (opt_console.compare("all") == 0) {
        console::console_driver_add(&console::arch_early_console);
        drvman->register_driver(console::VGAConsole::probe);
    } else {
        return false;
    }
    return true;
}
