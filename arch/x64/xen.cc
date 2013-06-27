#include "xen.hh"
#include "debug.hh"
#include "mmu.hh"
#include "processor.hh"
#include "cpuid.hh"

namespace xen {

// we only have asm constraints for the first three hypercall args,
// so we only inline hypercalls with <= 3 args.  The others are out-of-line,
// implemented in normal asm.
inline ulong hypercall(unsigned type, ulong a1, ulong a2, ulong a3)
{
    ulong ret;
    asm volatile("call *%[hyp]"
                 : "=a"(ret), "+D"(a1), "+S"(a2), "+d"(a3)
                 : [hyp]"c"(hypercall_page + 32 * type)
                 : "memory", "r10", "r8");
    return ret;
}

inline ulong hypercall(unsigned type)
{
    return hypercall(type, 0, 0, 0);
}

inline ulong hypercall(unsigned type, ulong a1)
{
    return hypercall(type, a1, 0, 0);
}

inline ulong hypercall(unsigned type, ulong a1, ulong a2)
{
    return hypercall(type, a1, a2, 0);
}

// some template magic to auto-cast pointers to ulongs in hypercall().
inline ulong cast_pointer(ulong v)
{
    return v;
}

inline ulong cast_pointer(void* p)
{
    return reinterpret_cast<ulong>(p);
}

template <typename... T>
inline ulong
memory_hypercall(unsigned type, T... args)
{
    return hypercall(__HYPERVISOR_memory_op, type, cast_pointer(args)...);
}

template <typename... T>
inline ulong
version_hypercall(unsigned type, T... args)
{
    return hypercall(__HYPERVISOR_xen_version, type, cast_pointer(args)...);
}

struct xen_shared_info xen_shared_info __attribute__((aligned(4096)));

static bool xen_pci_enabled()
{
    u16 magic = processor::inw(0x10);
    if (magic != 0x49d2) {
        return false;
    }

    u8 version = processor::inw(0x12);

    if (version != 0) {
        processor::outw(0xffff, 0x10); // product: experimental
        processor::outl(0, 0x10); // build id: whatever
        u16 _magic = processor::inw(0x10); // just make sure we are not blacklisted
        if (_magic != 0x49d2) {
            return false;
        }
    }
    processor::outw(3 ,0x10); // 2 => NICs, 1 => BLK
    return true;
}

void xen_init(processor::features_type &features, unsigned base)
{
        // Base + 1 would have given us the version number, it is mostly
        // uninteresting for us now
        auto x = processor::cpuid(base + 2);
        processor::wrmsr(x.b, cast_pointer(&hypercall_page));

        struct xen_feature_info info;
        info.submap_idx = 0;
        // 6 => get features
        if (version_hypercall(XENVER_get_features, &info) < 0)
            assert(0);

        features.xen_clocksource = (info.submap >> 9) & 1;

        struct xen_add_to_physmap map;
        map.domid = DOMID_SELF;
        map.idx = 0;
        map.space = 0;
        map.gpfn = cast_pointer(&xen_shared_info) >> 12;

        // 7 => add to physmap
        if (memory_hypercall(XENMEM_add_to_physmap, &map))
            assert(0);

        features.xen_pci = xen_pci_enabled();
}
}
