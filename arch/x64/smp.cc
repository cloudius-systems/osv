/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/drivers_config.h>
#include <smp.hh>
#include "processor.hh"
#include "msr.hh"
#include "apic.hh"
#include "ioapic.hh"
#include <osv/mmu.hh>
#include <string.h>
#if CONF_drivers_acpi
extern "C" {
#include "acpi.h"
}
#include <drivers/acpi.hh>
#endif
#include <boost/intrusive/parent_from_member.hpp>
#include <osv/debug.hh>
#include <osv/sched.hh>
#include <osv/barrier.hh>
#include <osv/prio.hh>
#include "osv/percpu.hh"
#include <osv/aligned_new.hh>
#include <osv/export.h>

extern "C" { void smp_main(void); }

extern u32 smpboot_cr0, smpboot_cr4;
extern u64 smpboot_efer, smpboot_cr3;
extern init_stack* smp_stack_free;

extern char smpboot[], smpboot_end[];

using namespace processor;

extern bool smp_allocator;
OSV_LIBSOLARIS_API
volatile unsigned smp_processors = 1;

using boost::intrusive::get_parent_from_member;

static void register_cpu(unsigned cpu_id, u32 apic_id, u32 acpi_id = 0)
{
    auto c = new sched::cpu(cpu_id);
    c->arch.apic_id = apic_id;
    c->arch.acpi_id = acpi_id;
    c->arch.initstack.next = smp_stack_free;
    smp_stack_free = &c->arch.initstack;
    sched::cpus.push_back(c);
}

#if CONF_drivers_acpi
void parse_madt()
{
    char madt_sig[] = ACPI_SIG_MADT;
    ACPI_TABLE_HEADER* madt_header;
    auto st = AcpiGetTable(madt_sig, 0, &madt_header);
    assert(st == AE_OK);
    auto madt = get_parent_from_member(madt_header, &ACPI_TABLE_MADT::Header);
    void* subtable = madt + 1;
    void* madt_end = static_cast<void*>(madt) + madt->Header.Length;
    unsigned nr_cpus = 0;
    while (subtable != madt_end) {
        auto s = static_cast<ACPI_SUBTABLE_HEADER*>(subtable);
        switch (s->Type) {
        case ACPI_MADT_TYPE_LOCAL_APIC: {
            auto lapic = get_parent_from_member(s, &ACPI_MADT_LOCAL_APIC::Header);
            if (!(lapic->LapicFlags & ACPI_MADT_ENABLED)) {
                break;
            }
            register_cpu(nr_cpus++, lapic->Id, lapic->ProcessorId);
            break;
        }
        case ACPI_MADT_TYPE_LOCAL_X2APIC: {
            auto x2apic = get_parent_from_member(s, &ACPI_MADT_LOCAL_X2APIC::Header);
            if (!(x2apic->LapicFlags & ACPI_MADT_ENABLED)) {
                break;
            }
            register_cpu(nr_cpus++, x2apic->LocalApicId, x2apic->Uid);
            break;
        }
        default:
            break;
        }
        subtable += s->Length;
    }
    if (!nr_cpus) { // No MP table was found or no cpu was found in there -> assume uni-processor
        register_cpu(nr_cpus++, 0);
    }
    debug(fmt("%d CPUs detected\n") % nr_cpus);
}
#endif

#define MPF_IDENTIFIER (('_'<<24) | ('P'<<16) | ('M'<<8) | '_')
struct mpf_structure {
    char signature[4];
    uint32_t configuration_table;
    uint8_t length;    // In 16 bytes (e.g. 1 = 16 bytes, 2 = 32 bytes)
    uint8_t specification_revision;
    uint8_t checksum;  // This value should make all bytes in the table equal 0 when added together
    uint8_t default_configuration; // If this is not zero then configuration_table should be
                                   // ignored and a default configuration should be loaded instead
    uint32_t features; // If bit 7 is then the IMCR is present and PIC mode is being used, otherwise
                       // virtual wire mode is; all other bits are reserved
} __attribute__((packed));

#define MP_TABLE_IDENTIFIER (('P'<<24) | ('M'<<16) | ('C'<<8) | 'P')
struct mp_table {
    char signature[4]; // "PCMP"
    uint16_t length;
    uint8_t mp_specification_revision;
    uint8_t checksum;  // Again, the byte should be all bytes in the table add up to 0
    char oem_id[8];
    char product_id[12];
    uint32_t oem_table;
    uint16_t oem_table_size;
    uint16_t entry_count;   // This value represents how many entries are following this table
    uint32_t lapic_address; // This is the memory mapped address of the local APICs
    uint16_t extended_table_length;
    uint8_t extended_table_checksum;
    uint8_t reserved;
} __attribute__((packed));

struct mp_processor {
    uint8_t type;  // Always 0
    uint8_t local_apic_id;
    uint8_t local_apic_version;
    uint8_t flags; // If bit 0 is clear then the processor must be ignored
                   // If bit 1 is set then the processor is the bootstrap processor
    uint32_t signature;
    uint32_t feature_flags;
    uint64_t reserved;
} __attribute__((packed));

static mp_table *find_mp_table(unsigned long base, long length)
{
    // First find MP floating pointer structure in the physical memory
    // region specified by the base and length
    void *addr = mmu::phys_to_virt(base);
    while (length > 0) {
       if (*static_cast<uint32_t *>(addr) == MPF_IDENTIFIER) {
           // We found the MP floating pointer structure
           auto mpf_struct = static_cast<mpf_structure*>(addr);
           // Now let us dereference physical address of MP table itself,
           // check signature and return its virtual address
           void *mp_table_addr = mmu::phys_to_virt(mpf_struct->configuration_table);
           if (*static_cast<uint32_t *>(mp_table_addr) == MP_TABLE_IDENTIFIER) {
               return static_cast<mp_table*>(mp_table_addr);
           }
           else {
               return nullptr;
           }
       }

       addr += 16;
       length -= 16;
    }
    return nullptr;
}

#define LAST_KB_IN_BASE_MEMORY_ADDR  639 * 0x400
#define FIRST_KB_IN_BASE_MEMORY_ADDR 0x0
#define NON_PROCESSOR_ENTRY_SIZE     8
void parse_mp_table()
{
    // Parse information about all vCPUs from MP table. For details please see
    // https://wiki.osdev.org/Symmetric_Multiprocessing#Finding_information_using_MP_Table
    // or http://www.osdever.net/tutorials/view/multiprocessing-support-for-hobby-oses-explained
    mp_table *table = find_mp_table(LAST_KB_IN_BASE_MEMORY_ADDR, 0x400);
    if (!table) {
        table = find_mp_table(FIRST_KB_IN_BASE_MEMORY_ADDR, 0x400);
    }

    unsigned nr_cpus = 0;
    if (table) {
        void *mp_entries = static_cast<void*>(table) + sizeof(mp_table);
        int entries_size = table->length - sizeof(mp_table);

        while (entries_size > 0) {
            int entry_size = NON_PROCESSOR_ENTRY_SIZE;
            auto proc_desc = static_cast<mp_processor*>(mp_entries);
            if (proc_desc->type == 0) {
                register_cpu(nr_cpus++, proc_desc->local_apic_id);
                entry_size = sizeof(mp_processor);
            }
            entries_size -= entry_size;
            mp_entries += entry_size;
        }
    }

    if (!nr_cpus) { // No MP table was found or no cpu was found in there -> assume uni-processor
        register_cpu(nr_cpus++, 0);
    }

    debug(fmt("%d CPUs detected\n") % nr_cpus);
}

void smp_init()
{
#if CONF_drivers_acpi
    if (acpi::is_enabled()) {
        parse_madt();
    } else {
#endif
        parse_mp_table();
#if CONF_drivers_acpi
    }
#endif

    sched::current_cpu = sched::cpus[0];
    for (auto c : sched::cpus) {
        c->incoming_wakeups = aligned_array_new<sched::cpu::incoming_wakeup_queue>(sched::cpus.size());
    }
    smpboot_cr0 = read_cr0();
    smpboot_cr4 = read_cr4();
    smpboot_efer = rdmsr(msr::IA32_EFER);
    smpboot_cr3 = read_cr3();
    memcpy(mmu::phys_to_virt(0), smpboot, smpboot_end - smpboot);
}

void ap_bringup(sched::cpu* c)
{
    __sync_fetch_and_add(&smp_processors, 1);
    processor::kvm_pv_eoi_init();
    c->idle_thread->start();
    c->load_balance();
}

sched::cpu* smp_initial_find_current_cpu()
{
    for (auto c : sched::cpus) {
        if (c->arch.apic_id == apic->id()) {
            return c;
        }
    }
    abort();
}

void smp_launch()
{
    ioapic::init();
    processor::kvm_pv_eoi_init();
    auto boot_cpu = smp_initial_find_current_cpu();
    for (auto c : sched::cpus) {
        auto name = osv::sprintf("balancer%d", c->id);
        if (c == boot_cpu) {
            sched::thread::current()->_detached_state->_cpu = c;
            // c->init_on_cpu() already done in main().
            (new sched::thread([c] { c->load_balance(); },
                    sched::thread::attr().pin(c).name(name)))->start();
            c->init_idle_thread();
            c->idle_thread->start();
            continue;
        }
        sched::thread::attr attr;
        attr.stack(81920).pin(c).name(name);
        c->init_idle_thread();
        c->bringup_thread = new sched::thread([=] { ap_bringup(c); }, attr, true);

        apic->init_ipi(c->arch.apic_id, 0x4500); // INIT
        apic->init_ipi(c->arch.apic_id, 0x4600); // SIPI
        apic->init_ipi(c->arch.apic_id, 0x4600); // SIPI
    }

    while (smp_processors != sched::cpus.size()) {
        barrier();
    }
}

void smp_main()
{
    apic->init_on_ap();
    sched::cpu* cpu = smp_initial_find_current_cpu();
    assert(cpu);
    cpu->init_on_cpu();
    cpu->bringup_thread->_detached_state->_cpu = cpu;
    cpu->bringup_thread->switch_to_first();
}

void smp_crash_other_processors()
{
    if (apic && smp_processors > 1) {
        apic->nmi_allbutself();
    }
}
