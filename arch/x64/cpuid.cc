#include "cpuid.hh"
#include "processor.hh"

namespace processor {

features_type features;

namespace {

struct signature {
    u32 b;
    u32 c;
    u32 d;
};

struct cpuid_bit {
    unsigned leaf;
    char word;
    unsigned bit;
    bool features_type::*flag;
    unsigned subleaf;
    signature* sign;
};

signature kvm_signature = {
    0x4b4d564b, 0x564b4d56, 0x4d
};

typedef features_type f;

cpuid_bit cpuid_bits[] = {
    { 1, 'c', 0, &f::sse3 },
    { 1, 'c', 9, &f::ssse3 },
    { 1, 'c', 13, &f::cmpxchg16b },
    { 1, 'c', 19, &f::sse4_1 },
    { 1, 'c', 20, &f::sse4_2 },
    { 1, 'c', 21, &f::x2apic },
    { 1, 'c', 24, &f::tsc_deadline },
    { 1, 'c', 27, &f::xsave },
    { 1, 'c', 28, &f::avx },
    { 1, 'c', 30, &f::rdrand },
    { 7, 'b', 0, &f::fsgsbase, 0 },
    { 7, 'b', 9, &f::repmovsb, 0 },
    { 0x80000001, 'd', 26, &f::gbpage },
    { 0x80000007, 'd', 8, &f::invariant_tsc },
    { 0x40000001, 'a', 0, &f::kvm_clocksource, 0, &kvm_signature },
    { 0x40000001, 'a', 3, &f::kvm_clocksource2, 0, &kvm_signature },
    { 0x40000001, 'a', 24, &f::kvm_clocksource_stable, 0, &kvm_signature },
};

constexpr unsigned nr_cpuid_bits = sizeof(cpuid_bits) / sizeof(*cpuid_bits);

void process_cpuid_bit(const cpuid_bit& b)
{
    bool subleaf = b.leaf == 7;
    auto base = b.leaf & 0xf0000000;
    auto s = b.sign;
    if (s) {
        auto x = cpuid(base);
        if (x.b != s->b || x.c != s->c || x.d != s->d) {
            return;
        }
    }
    unsigned max = cpuid(base).a;
    if (base == 0x40000000 && s == &kvm_signature && max == 0) {
        max = 0x40000001; // workaround kvm bug
    }
    if (b.leaf > max) {
        return;
    }
    if (subleaf && b.subleaf > cpuid(b.leaf, 0).a) {
        return;
    }
    static unsigned cpuid_result::*regs[] = {
        &cpuid_result::a, &cpuid_result::b, &cpuid_result::c, &cpuid_result::d
    };
    auto w = cpuid(b.leaf, b.subleaf).*regs[b.word - 'a'];
    features.*(b.flag) = (w >> b.bit) & 1;
}


void  __attribute__((constructor(200))) process_cpuid()
{
    for (unsigned i = 0; i < nr_cpuid_bits; ++i) {
        process_cpuid_bit(cpuid_bits[i]);
    }
}

}

}
