/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Minimal Intel architectural-perfmon readout.  See include/osv/pmu.hh for the
// scope and caveats.  Everything here operates on the CURRENT CPU's per-CPU
// perfmon MSRs; there is no per-thread virtualization, so callers pin the
// measured thread.

#include <osv/pmu.hh>
#include "processor.hh"
#include <stdio.h>

using namespace processor;

namespace pmu {

// Perfmon MSRs (Intel SDM vol 4).  Kept local so the diff does not touch the
// shared msr.hh enum and stays self-contained / openable on pure master.
enum : u32 {
    IA32_PMC0                 = 0x0c1,
    IA32_PERFEVTSEL0          = 0x186,
    IA32_FIXED_CTR0           = 0x309,
    IA32_FIXED_CTR_CTRL       = 0x38d,
    IA32_PERF_GLOBAL_STATUS   = 0x38e,
    IA32_PERF_GLOBAL_CTRL     = 0x38f,
    IA32_PERF_GLOBAL_OVF_CTRL = 0x390,
};

// PERFEVTSEL bit fields.
enum : u64 {
    EVTSEL_USR   = 1ull << 16,
    EVTSEL_OS    = 1ull << 17,
    EVTSEL_EN    = 1ull << 22,
};

// RDPMC index for the fixed-function counters.
static constexpr u32 RDPMC_FIXED = 1u << 30;

// An event's (event-select, umask) for a given family, or unavailable.
struct encoding {
    bool ok;
    u8   sel;
    u8   umask;
};

// The programmable event encodings that differ across families; the fixed
// events (instructions/cycles) do not use PERFEVTSEL at all.
struct family_table {
    const char* name;
    encoding itlb_walk;    // ITLB_MISSES.WALK_COMPLETED
    encoding dtlb_walk;    // DTLB_LOAD_MISSES.WALK_COMPLETED
    encoding l3_miss;      // MEM_LOAD_RETIRED.L3_MISS
    encoding l2_miss;      // MEM_LOAD_RETIRED.L2_MISS
    encoding icache_stall; // instruction-fetch stall cycles
    encoding ept_walk;     // EPT (nested-paging) walk pending cycles
};

// Intel encodings by microarchitecture.  Sources: Intel SDM vol 3B perfmon
// tables / perfmon-events.intel.com.  WALK_COMPLETED umasks:
//   Sandy/Ivy Bridge:    ITLB_MISSES 0x85/0x02, DTLB_LOAD_MISSES 0x08/0x02
//   Haswell/Broadwell:   same event/umask 0x85/0x02, 0x08/0x02
//   Skylake and later:   ITLB_MISSES 0x85/0x0e (WALK_COMPLETED = all sizes),
//                        DTLB_LOAD_MISSES 0x08/0x0e
//   MEM_LOAD_RETIRED.L3_MISS: 0xd1/0x20 on Haswell+; Sandy/Ivy use
//                        MEM_LOAD_UOPS_RETIRED.LLC_MISS 0xd1/0x40.
//   MEM_LOAD_RETIRED.L2_MISS: 0xd1/0x10 (Haswell+); Sandy/Ivy 0xd1/0x02.
//   i-cache fetch stall: Skylake+ ICACHE_16B.IFDATA_STALL / ICACHE_DATA.STALLS
//                        0x80/0x04; Haswell ICACHE.IFETCH_STALL 0x80/0x04;
//                        Sandy/Ivy ICACHE.MISSES 0x80/0x02.
//   EPT.WALK_PENDING (nested-paging walk cycles): 0x4f/0x10 Skylake+/Haswell.
//                        Typically counts only when the guest owns the PMU AND
//                        the VMM passes EPT events through -- under KVM -cpu
//                        host it is usually host-only, so a guest read of ~0
//                        alongside non-zero iTLB is the "host-only" finding.
static const family_table fam_skylake_plus = {
    "skylake+",
    { true, 0x85, 0x0e }, { true, 0x08, 0x0e }, { true, 0xd1, 0x20 },
    { true, 0xd1, 0x10 }, { true, 0x80, 0x04 }, { true, 0x4f, 0x10 },
};
static const family_table fam_haswell = {
    "haswell/broadwell",
    { true, 0x85, 0x02 }, { true, 0x08, 0x02 }, { true, 0xd1, 0x20 },
    { true, 0xd1, 0x10 }, { true, 0x80, 0x04 }, { true, 0x4f, 0x10 },
};
static const family_table fam_sandy = {
    "sandy/ivy-bridge",
    { true, 0x85, 0x02 }, { true, 0x08, 0x02 }, { true, 0xd1, 0x40 },
    { true, 0xd1, 0x02 }, { true, 0x80, 0x02 }, { true, 0x4f, 0x10 },
};
// Unknown family: fixed counters still work (they need no encoding); the
// programmable events report unavailable rather than arm a guess.
static const family_table fam_unknown = {
    "unknown",
    { false, 0, 0 }, { false, 0, 0 }, { false, 0, 0 },
    { false, 0, 0 }, { false, 0, 0 }, { false, 0, 0 },
};

static capabilities _caps;
static const family_table* _fam = &fam_unknown;
static bool _probed = false;

// Resolve DisplayModel per Intel's family-6 rule and pick the encoding table.
static const family_table* pick_family(u32 model, u32 family)
{
    if (family != 6) {
        return &fam_unknown;
    }
    switch (model) {
    // Sandy Bridge / Ivy Bridge
    case 0x2a: case 0x2d: case 0x3a: case 0x3e:
        return &fam_sandy;
    // Haswell / Broadwell
    case 0x3c: case 0x45: case 0x46: case 0x3f:
    case 0x3d: case 0x47: case 0x4f: case 0x56:
        return &fam_haswell;
    // Skylake, Kaby/Coffee/Comet Lake, Cascade/Ice/Sapphire/Emerald server,
    // Cannon, Tiger, Alder/Raptor -- WALK_COMPLETED umask 0x0e applies.
    case 0x4e: case 0x5e: case 0x55: case 0x8e: case 0x9e:
    case 0xa5: case 0xa6: case 0x66: case 0x6a: case 0x6c:
    case 0x7d: case 0x7e: case 0x8c: case 0x8d: case 0x97:
    case 0x9a: case 0xbf: case 0x8f: case 0xcf:
        return &fam_skylake_plus;
    default:
        // Newer/unknown family-6: umask 0x0e has been stable since Skylake, so
        // it is the safest default -- but say so via the "skylake+" family name
        // printed at boot, so a wrong guess is visible, not silent.
        return &fam_skylake_plus;
    }
}

static void probe()
{
    if (_probed) {
        return;
    }
    _probed = true;

    // CPUID leaf 0xA: architectural performance monitoring.
    auto a = cpuid(0xa);
    u8 version = a.a & 0xff;
    if (version == 0) {
        // No architectural PMU (the ordinary non-metal Nitro case).  Leave
        // _caps zeroed so available() is false and nothing ever arms.
        return;
    }
    _caps.version        = version;
    _caps.gp_counters    = (a.a >> 8) & 0xff;
    _caps.gp_width       = (a.a >> 16) & 0xff;
    _caps.fixed_counters = a.d & 0x1f;
    _caps.fixed_width    = (a.d >> 5) & 0xff;

    auto s = cpuid(1);
    u32 base_family = (s.a >> 8) & 0xf;
    u32 base_model  = (s.a >> 4) & 0xf;
    u32 ext_model   = (s.a >> 16) & 0xf;
    u32 family = base_family;
    u32 model = base_model;
    if (base_family == 6 || base_family == 0xf) {
        model = base_model | (ext_model << 4);
    }
    _fam = pick_family(model, family);
    _caps.family = _fam->name;
}

bool available()
{
    probe();
    return _caps.version >= 1;
}

const capabilities& caps()
{
    probe();
    return _caps;
}

const char* name(event ev)
{
    switch (ev) {
    case event::instructions_retired:     return "instructions-retired";
    case event::unhalted_cycles:          return "unhalted-cycles";
    case event::icache_stalls:            return "ICACHE.IFETCH_STALL";
    case event::l2_miss:                  return "MEM_LOAD_RETIRED.L2_MISS";
    case event::l3_miss:                  return "MEM_LOAD_RETIRED.L3_MISS";
    case event::itlb_walk_completed:      return "ITLB_MISSES.WALK_COMPLETED";
    case event::dtlb_load_walk_completed: return "DTLB_LOAD_MISSES.WALK_COMPLETED";
    case event::ept_walk_cycles:          return "EPT.WALK_PENDING";
    }
    return "?";
}

// Enable rdpmc from any CPL (OSv is CPL0 so this is optional, but the brief
// asks us to actually SET the defined-but-unset cr4_pce bit).
static void enable_rdpmc()
{
    ulong cr4 = read_cr4();
    if (!(cr4 & cr4_pce)) {
        write_cr4(cr4 | cr4_pce);
    }
}

// Which RDPMC index and fixed-counter slot a fixed event uses.
static bool fixed_slot(event ev, u32& slot)
{
    if (ev == event::instructions_retired) { slot = 0; return true; }
    if (ev == event::unhalted_cycles)      { slot = 1; return true; }
    return false;
}

static const encoding* prog_encoding(event ev)
{
    switch (ev) {
    case event::icache_stalls:            return &_fam->icache_stall;
    case event::l2_miss:                  return &_fam->l2_miss;
    case event::l3_miss:                  return &_fam->l3_miss;
    case event::itlb_walk_completed:      return &_fam->itlb_walk;
    case event::dtlb_load_walk_completed: return &_fam->dtlb_walk;
    case event::ept_walk_cycles:          return &_fam->ept_walk;
    default:                              return nullptr;
    }
}

// Per-CPU armed state so read() knows how to fetch the count.
static __thread bool _armed_fixed;
static __thread u32  _armed_slot;

bool program(event ev)
{
    if (!available()) {
        return false;
    }
    enable_rdpmc();

    // Stop everything while we reconfigure.
    wrmsr(IA32_PERF_GLOBAL_CTRL, 0);

    u32 slot;
    if (fixed_slot(ev, slot)) {
        if (slot >= _caps.fixed_counters) {
            return false;
        }
        // FIXED_CTR_CTRL: 4 bits per fixed counter; 0x3 = enable, OS+USR.
        u64 fc = rdmsr(IA32_FIXED_CTR_CTRL);
        fc &= ~(0xfull << (slot * 4));
        fc |=  (0x3ull << (slot * 4));
        wrmsr(IA32_FIXED_CTR_CTRL, fc);
        wrmsr(IA32_FIXED_CTR0 + slot, 0);
        // GLOBAL_CTRL fixed-counter enables live at bit 32+slot.
        wrmsr(IA32_PERF_GLOBAL_CTRL, 1ull << (32 + slot));
        _armed_fixed = true;
        _armed_slot = slot;
        return true;
    }

    const encoding* e = prog_encoding(ev);
    if (!e || !e->ok || _caps.gp_counters < 1) {
        return false;
    }
    // General counter 0.
    u64 sel = e->sel | ((u64)e->umask << 8) | EVTSEL_OS | EVTSEL_USR | EVTSEL_EN;
    wrmsr(IA32_PMC0, 0);
    wrmsr(IA32_PERFEVTSEL0, sel);
    wrmsr(IA32_PERF_GLOBAL_CTRL, 1ull << 0);
    _armed_fixed = false;
    _armed_slot = 0;
    return true;
}

u64 read()
{
    if (!available()) {
        return 0;
    }
    if (_armed_fixed) {
        return rdpmc(RDPMC_FIXED | _armed_slot);
    }
    return rdpmc(0);
}

void print_proof()
{
    probe();
    if (_caps.version == 0) {
        printf("PMUPROOF version=0 gp_counters=0 width=0 fixed=0 family=none "
               "(no architectural PMU; pmu::available()=false)\n");
        return;
    }
    printf("PMUPROOF version=%u gp_counters=%u width=%u fixed=%u family=%s\n",
           _caps.version, _caps.gp_counters, _caps.gp_width,
           _caps.fixed_counters, _caps.family);
}

}
