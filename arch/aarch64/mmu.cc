
namespace mmu {

void flush_tlb_all() {
    asm volatile("dsb sy; tlbi vmalle1is; dsb sy; isb;");
}

void flush_tlb_local() {
    asm volatile("dsb sy; tlbi vmalle1; dsb sy; isb;");
}

}
