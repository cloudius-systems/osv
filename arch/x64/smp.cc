#include "smp.hh"
#include "processor.hh"
#include "msr.hh"
#include "apic.hh"
#include "mmu.hh"
#include <string.h>
extern "C" {
#include "acpi.h"
}
#include <boost/intrusive/parent_from_member.hpp>
#include "debug.hh"
#include "sched.hh"
#include "barrier.hh"

extern "C" { void smp_main(void); }

extern u32 smpboot_cr0, smpboot_cr4;
extern u64 smpboot_efer, smpboot_cr3;
extern init_stack* smp_stack_free;

extern char smpboot[], smpboot_end[];

using namespace processor;

volatile unsigned smp_processors = 1;

using boost::intrusive::get_parent_from_member;

void parse_madt()
{
    auto st = AcpiInitializeTables(NULL, 0, false);
    assert(st == AE_OK);
    char madt_sig[] = ACPI_SIG_MADT;
    ACPI_TABLE_HEADER* madt_header;
    st = AcpiGetTable(madt_sig, 0, &madt_header);
    assert(st == AE_OK);
    auto madt = get_parent_from_member(madt_header, &ACPI_TABLE_MADT::Header);
    void* subtable = madt + 1;
    void* madt_end = static_cast<void*>(madt) + madt->Header.Length;
    unsigned idgen = 0;
    while (subtable != madt_end) {
        auto s = static_cast<ACPI_SUBTABLE_HEADER*>(subtable);
        switch (s->Type) {
        case ACPI_MADT_TYPE_LOCAL_APIC: {
            auto lapic = get_parent_from_member(s, &ACPI_MADT_LOCAL_APIC::Header);
            if (!(lapic->LapicFlags & ACPI_MADT_ENABLED)) {
                break;
            }
            auto c = new sched::cpu;
            c->id = idgen++;
            c->arch.apic_id = lapic->Id;
            c->arch.acpi_id = lapic->ProcessorId;
            c->arch.initstack.next = smp_stack_free;
            smp_stack_free = &c->arch.initstack;
            debug(fmt("acpi %d apic %d\n") % c->arch.acpi_id % c->arch.apic_id);
            sched::cpus.push_back(c);
            break;
        }
        default:
            break;
        }
        subtable += s->Length;
    }
}

void smp_init()
{
    parse_madt();
    for (auto c : sched::cpus) {
        c->incoming_wakeups = new sched::cpu::incoming_wakeup_queue[sched::cpus.size()];
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
    c->idle_thread.start();
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
    auto boot_cpu = smp_initial_find_current_cpu();
    for (auto c : sched::cpus) {
        if (c == boot_cpu) {
            sched::thread::current()->_cpu = c;
            c->init_on_cpu();
            (new sched::thread([c] { c->load_balance(); },
                    sched::thread::attr(c)))->start();
            c->idle_thread.start();
            continue;
        }
        sched::thread::attr attr;
        attr.stack = { new char[81920], 81920 };
        attr.pinned_cpu = c;
        c->bringup_thread = new sched::thread([=] { ap_bringup(c); }, attr, true);
    }
    apic->write(apicreg::ICR, 0xc4500); // INIT
    apic->write(apicreg::ICR, 0xc4600); // SIPI
    apic->write(apicreg::ICR, 0xc4600); // SIPI
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
    cpu->bringup_thread->_cpu = cpu;
    cpu->bringup_thread->switch_to_first();
}

void crash_other_processors()
{
    if (apic) {
        apic->nmi_allbutself();
    }
}
