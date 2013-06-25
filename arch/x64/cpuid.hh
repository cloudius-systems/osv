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
    bool fsgsbase;
    bool repmovsb;
    bool gbpage;
    bool invariant_tsc;
    bool kvm_clocksource;
    bool kvm_clocksource2;
    bool kvm_clocksource_stable;
    bool xen_clocksource;
    bool xen_pci;
};

extern const features_type& features();

}


#endif /* CPUID_HH_ */
