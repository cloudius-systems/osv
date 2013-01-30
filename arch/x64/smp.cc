#include "smp.hh"
#include "processor.hh"
#include "msr.hh"
#include "apic.hh"
#include "mmu.hh"
#include <string.h>

extern "C" { void smp_main(void); }

extern u32 smpboot_cr0, smpboot_cr4;
extern u64 smpboot_efer;

extern char smpboot[], smpboot_end[];

using namespace processor;

volatile unsigned smp_processors = 1;

void smp_init()
{
    smpboot_cr0 = read_cr0();
    smpboot_cr4 = read_cr4();
    smpboot_efer = rdmsr(msr::IA32_EFER);
    memcpy(mmu::phys_to_virt(0), smpboot, smpboot_end - smpboot);
    apic->write(apicreg::ICR, 0xc4500); // INIT
    apic->write(apicreg::ICR, 0xc4600); // SIPI
    apic->write(apicreg::ICR, 0xc4600); // SIPI
}

void smp_main()
{
    __sync_fetch_and_add(&smp_processors, 1);
    abort();
}
