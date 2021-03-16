/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef AARCH64_PROCESSOR_HH
#define AARCH64_PROCESSOR_HH

#include <osv/types.h>
#include <osv/debug.h>
#include <cstddef>

namespace processor {

constexpr unsigned daif_i = 1 << 7;
/* mask away everything but affinity 3, aff 2 and aff 1 to identify cpus */
constexpr u64 mpidr_mask = 0x000000ff00ffffffULL;

inline void wfi()
{
    asm volatile ("wfi" ::: "memory");
}

inline void irq_enable()
{
    asm volatile ("msr daifclr, #2; isb; " ::: "memory");
}

inline void irq_disable()
{
    asm volatile ("msr daifset, #2; isb; " ::: "memory");
}

__attribute__((no_instrument_function))
inline void irq_disable_notrace();

inline void irq_disable_notrace()
{
    asm volatile ("msr daifset, #2; isb; " ::: "memory");
}

inline void wait_for_interrupt() {
    irq_enable();
    wfi();
}

inline void halt_no_interrupts() {
    irq_disable();
    while (1) {
        wfi();
    }
}
inline u64 read_ttbr0() {
    u64 val;
    asm volatile("mrs %0, ttbr0_el1; isb;" : "=r"(val) :: "memory");
    return val;
}
inline void write_ttbr0(u64 val) {
    asm volatile("msr ttbr0_el1, %0; isb;" :: "r"(val) : "memory");
}
inline u64 read_ttbr1() {
    u64 val;
    asm volatile("mrs %0, ttbr1_el1; isb;" : "=r"(val) :: "memory");
    return val;
}
inline void write_ttbr1(u64 val) {
    asm volatile("msr ttbr1_el1, %0; isb;" :: "r"(val) : "memory");
}

inline u64 read_mpidr()
{
    u64 mpidr;
    asm volatile("mrs %0, mpidr_el1; isb;" : "=r" (mpidr));
    return mpidr & mpidr_mask;
}

/* the user of ticks() just wants a high resolution counter.
 * We just read the virtual counter, since using the Performance
 * Monitors to get the actual clock cycles has implications:
 * they are an optional component of the Architecture, and they
 * can have an impact on performance.
 */
inline u64 ticks()
{
    u64 cntvct;
    asm volatile ("isb; mrs %0, cntvct_el0" : "=r"(cntvct));
    return cntvct;
}

// Keep this in sync with fpu_state_save/load in arch/aarch64/entry.S
struct fpu_state {
    __uint128_t vregs[32];
    u32 fpsr;
    u32 fpcr;
    // 64 bits of implied padding here
};
static_assert(sizeof(struct fpu_state) == 528, "Wrong size for struct fpu_state");
static_assert(offsetof(fpu_state, fpsr) == 512, "Wrong offset for fpsr");
static_assert(offsetof(fpu_state, fpcr) == 516, "Wrong offset for fpcr");

extern "C" {
void fpu_state_save(fpu_state *s);
void fpu_state_load(fpu_state *s);
}

}

#endif /* AARCH64_PROCESSOR_HH */
