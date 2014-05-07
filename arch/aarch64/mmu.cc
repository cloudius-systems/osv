#include <osv/mmu.hh>

namespace mmu {

void flush_tlb_all() {
    asm volatile("dsb sy; tlbi vmalle1is; dsb sy; isb;");
}

void flush_tlb_local() {
    asm volatile("dsb sy; tlbi vmalle1; dsb sy; isb;");
}

static pt_element page_table_root[2] __attribute__((init_priority((int)init_prio::pt_root)));

void switch_to_runtime_page_tables()
{
    auto pt_ttbr0 = mmu::page_table_root[0].next_pt_addr();
    auto pt_ttbr1 = mmu::page_table_root[1].next_pt_addr();
    asm volatile("msr ttbr0_el1, %0; isb;" ::"r" (pt_ttbr0));
    asm volatile("msr ttbr1_el1, %0; isb;" ::"r" (pt_ttbr1));
    mmu::flush_tlb_all();
}

pt_element *get_root_pt(uintptr_t virt)
{
    return &page_table_root[virt >> 63];
}

pt_element make_empty_pte() { return pt_element(); }

pt_element make_pte(phys addr, bool large,
                    unsigned perm = perm_read | perm_write | perm_exec)
{
    pt_element pte;
    pte.set_valid(perm != 0);
    pte.set_writable(perm & perm_write);
    pte.set_executable(perm & perm_exec);
    pte.set_dirty(true);
    pte.set_large(large);
    pte.set_addr(addr, large);

    arch_pt_element::set_user(&pte, false);
    arch_pt_element::set_accessed(&pte, true);
    /* at the moment we hardcode memory attributes,
       but the API would need to be adapted for device direct assignment */
    arch_pt_element::set_share(&pte, true);
    arch_pt_element::set_attridx(&pte, 4);

    return pte;
}

pt_element make_normal_pte(phys addr, unsigned perm)
{
    return make_pte(addr, false, perm);
}

pt_element make_large_pte(phys addr, unsigned perm)
{
    return make_pte(addr, true, perm);
}

bool is_page_fault_insn(unsigned int esr) {
    unsigned int ec = esr >> 26;
    return ec == 0x20 || ec == 0x21;
}

bool is_page_fault_write(unsigned int esr) {
    unsigned int ec = esr >> 26;
    return (ec == 0x24 || ec == 0x25) && (esr & 0x40);
}

bool is_page_fault_write_exclusive(unsigned int esr) {
    return is_page_fault_write(esr);
}

}
