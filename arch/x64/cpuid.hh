#ifndef CPUID_HH_
#define CPUID_HH_

namespace processor {

struct features_type {
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
};

extern features_type features;

}


#endif /* CPUID_HH_ */
