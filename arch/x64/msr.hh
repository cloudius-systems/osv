/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef MSR_HH_
#define MSR_HH_

#include "processor.hh"

enum class msr : uint32_t {
    X2APIC_ID = 0x802,
    X2APIC_VERSION = 0x803,
    X2APIC_TPR = 0x808,
    X2APIC_PPR = 0x80a,
    X2APIC_EOI = 0x80b,
    X2APIC_LDR = 0x80d,
    X2APIC_SVR = 0x80f,
    X2APIC_ISR0 = 0x810,
    X2APIC_ISR1 = 0x811,
    X2APIC_ISR2 = 0x812,
    X2APIC_ISR3 = 0x813,
    X2APIC_ISR4 = 0x814,
    X2APIC_ISR5 = 0x815,
    X2APIC_ISR6 = 0x816,
    X2APIC_ISR7 = 0x817,
    X2APIC_TMR0 = 0x818,
    X2APIC_TMR1 = 0x819,
    X2APIC_TMR2 = 0x81a,
    X2APIC_TMR3 = 0x81b,
    X2APIC_TMR4 = 0x81c,
    X2APIC_TMR5 = 0x81d,
    X2APIC_TMR6 = 0x81e,
    X2APIC_TMR7 = 0x81f,
    X2APIC_IRR0 = 0x820,
    X2APIC_IRR1 = 0x821,
    X2APIC_IRR2 = 0x822,
    X2APIC_IRR3 = 0x823,
    X2APIC_IRR4 = 0x824,
    X2APIC_IRR5 = 0x825,
    X2APIC_IRR6 = 0x826,
    X2APIC_IRR7 = 0x827,
    X2APIC_ESR = 0x828,
    X2APIC_LVT_CMCI = 0x82f,
    X2APIC_ICR = 0x830,
    X2APIC_LVT_TIMER = 0x832,
    X2APIC_LVT_THERMAL = 0x833,
    X2APIC_LVT_PERF = 0x834,
    X2APIC_LVT_LINT0 = 0x835,
    X2APIC_LVT_LINT1 = 0x836,
    X2APIC_LVT_ERROR = 0x837,
    X2APIC_TIMER_ICR = 0x838,
    X2APIC_TIMER_CCR = 0x839,
    X2APIC_TIMER_DCR = 0x83e,
    X2APIC_SELF_IPI = 0x83f,

    IA32_APIC_BASE = 0x0000001b,
    IA32_EFER = 0xc0000080,
    IA32_FS_BASE = 0xc0000100,

    KVM_WALL_CLOCK_NEW = 0x4b564d00,
    KVM_SYSTEM_TIME_NEW = 0x4b564d01,

};

namespace processor {

inline void wrmsr(msr index, u64 value)
{
    wrmsr(static_cast<u32>(index), value);
}

inline void wrmsr_safe(msr index, u64 value)
{
    wrmsr_safe(static_cast<u32>(index), value);
}

inline u64 rdmsr(msr index)
{
    return rdmsr(static_cast<u32>(index));
}

}

#endif /* MSR_HH_ */
