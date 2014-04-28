/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef CPUID_HH_
#define CPUID_HH_

namespace processor {

struct features_type {
    features_type();
    bool sse3;
    bool ssse3;
    bool cmpxchg16b;
    bool sse4_1;
    bool sse4_2;
    bool x2apic;
    bool tsc_deadline;
    bool xsave;
    bool avx;
    bool rdrand;
    bool clflush;
    bool fsgsbase;
    bool repmovsb;
    bool gbpage;
    bool invariant_tsc;
    bool kvm_clocksource;
    bool kvm_clocksource2;
    bool kvm_clocksource_stable;
    bool kvm_pv_eoi;
    bool xen_clocksource;
    bool xen_vector_callback;
    bool xen_pci;
};

extern const features_type& features();

}


#endif /* CPUID_HH_ */
