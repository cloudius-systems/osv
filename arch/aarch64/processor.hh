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

struct fpu_state {
    __uint128_t vregs[32];
    u32 fpsr;
    u32 fpcr;
};

inline void fpu_state_save(fpu_state *s)
{
    asm volatile("stp q0, q1, %0" : "=Ump"(s->vregs[0]) :: "memory");
    asm volatile("stp q2, q3, %0" : "=Ump"(s->vregs[2]) :: "memory");
    asm volatile("stp q4, q5, %0" : "=Ump"(s->vregs[4]) :: "memory");
    asm volatile("stp q6, q7, %0" : "=Ump"(s->vregs[6]) :: "memory");
    asm volatile("stp q8, q9, %0" : "=Ump"(s->vregs[8]) :: "memory");
    asm volatile("stp q10, q11, %0" : "=Ump"(s->vregs[10]) :: "memory");
    asm volatile("stp q12, q13, %0" : "=Ump"(s->vregs[12]) :: "memory");
    asm volatile("stp q14, q15, %0" : "=Ump"(s->vregs[14]) :: "memory");
    asm volatile("stp q16, q17, %0" : "=Ump"(s->vregs[16]) :: "memory");
    asm volatile("stp q18, q19, %0" : "=Ump"(s->vregs[18]) :: "memory");
    asm volatile("stp q20, q21, %0" : "=Ump"(s->vregs[20]) :: "memory");
    asm volatile("stp q22, q23, %0" : "=Ump"(s->vregs[22]) :: "memory");
    asm volatile("stp q24, q25, %0" : "=Ump"(s->vregs[24]) :: "memory");
    asm volatile("stp q26, q27, %0" : "=Ump"(s->vregs[26]) :: "memory");
    asm volatile("stp q28, q29, %0" : "=Ump"(s->vregs[28]) :: "memory");
    asm volatile("stp q30, q31, %0" : "=Ump"(s->vregs[30]) :: "memory");

    asm volatile("mrs %0, fpsr" : "=r"(s->fpsr) :: "memory");
    asm volatile("mrs %0, fpcr" : "=r"(s->fpcr) :: "memory");
}

inline void fpu_state_load(fpu_state *s)
{
    asm volatile("ldp q0, q1, %0" :: "Ump"(s->vregs[0]) : "q0", "q1");
    asm volatile("ldp q2, q3, %0" :: "Ump"(s->vregs[2]) : "q2", "q3");
    asm volatile("ldp q4, q5, %0" :: "Ump"(s->vregs[4]) : "q4", "q5");
    asm volatile("ldp q6, q7, %0" :: "Ump"(s->vregs[6]) : "q6", "q7");
    asm volatile("ldp q8, q9, %0" :: "Ump"(s->vregs[8]) : "q8", "q9");
    asm volatile("ldp q10, q11, %0" :: "Ump"(s->vregs[10]) : "q10", "q11");
    asm volatile("ldp q12, q13, %0" :: "Ump"(s->vregs[12]) : "q12", "q13");
    asm volatile("ldp q14, q15, %0" :: "Ump"(s->vregs[14]) : "q14", "q15");
    asm volatile("ldp q16, q17, %0" :: "Ump"(s->vregs[16]) : "q16", "q17");
    asm volatile("ldp q18, q19, %0" :: "Ump"(s->vregs[18]) : "q18", "q19");
    asm volatile("ldp q20, q21, %0" :: "Ump"(s->vregs[20]) : "q20", "q21");
    asm volatile("ldp q22, q23, %0" :: "Ump"(s->vregs[22]) : "q22", "q23");
    asm volatile("ldp q24, q25, %0" :: "Ump"(s->vregs[24]) : "q24", "q25");
    asm volatile("ldp q26, q27, %0" :: "Ump"(s->vregs[26]) : "q26", "q27");
    asm volatile("ldp q28, q29, %0" :: "Ump"(s->vregs[28]) : "q28", "q29");
    asm volatile("ldp q30, q31, %0" :: "Ump"(s->vregs[30]) : "q30", "q31");

    asm volatile("msr fpsr, %0" :: "r"(s->fpsr) : "memory");
    asm volatile("msr fpcr, %0" :: "r"(s->fpcr) : "memory");
}

}

#endif /* AARCH64_PROCESSOR_HH */
