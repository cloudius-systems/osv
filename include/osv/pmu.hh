/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Minimal hardware performance-counter (PMU) readout for OSv.
//
// This is a DIAGNOSTIC facility, not a perf(1) clone.  It programs Intel
// architectural-perfmon events on the CURRENT CPU (the two fixed counters, and
// general counter 0 for one programmable event) and reads the counts back.  It
// does NOT do sampling, overflow interrupts, counter multiplexing, or
// per-thread save/restore across preemption.  Because the counts live in
// per-CPU MSRs, a measured thread MUST stay pinned to one CPU for its counts to
// be valid.  x86_64 Intel only for now.
//
// See arch/x64/pmu.cc for the implementation and the per-family event table.

#ifndef OSV_PMU_HH
#define OSV_PMU_HH

#include <osv/types.h>
#include <osv/export.h>

namespace pmu {

// Events we can name.  Fixed events read the fixed-function counters; the rest
// are programmed onto general counter 0.  Encodings for the programmable events
// are resolved per CPU family (see arch/x64/pmu.cc); an event with no encoding
// for the running family reports unavailable rather than arming a wrong code.
// ept_walk_cycles counts nested-paging walks, which a hypervisor usually does
// not expose to its guest, so in a guest it often reads zero.
enum class event {
    instructions_retired,        // fixed ctr 0
    unhalted_cycles,             // fixed ctr 1
    icache_stalls,               // instruction-fetch stall cycles (i-cache)
    l2_miss,                     // MEM_LOAD_RETIRED.L2_MISS
    l3_miss,                     // MEM_LOAD_RETIRED.L3_MISS
    itlb_walk_completed,         // ITLB_MISSES.WALK_COMPLETED
    dtlb_load_walk_completed,    // DTLB_LOAD_MISSES.WALK_COMPLETED
    ept_walk_cycles,             // EPT (nested-paging) walk cycles; often host-only
};

// What CPUID leaf 0xA reported.  version==0 means no architectural PMU
// (the ordinary non-metal Nitro guest case).
struct capabilities {
    u8  version = 0;
    u8  gp_counters = 0;      // number of general-purpose counters
    u8  gp_width = 0;         // bit width of a general counter
    u8  fixed_counters = 0;   // number of fixed-function counters
    u8  fixed_width = 0;
    const char* family = "unknown";  // resolved microarch family name
};

// True only if CPUID leaf 0xA reports architectural perfmon version 2 or
// later, the first with IA32_PERF_GLOBAL_CTRL and fixed counters.  Everything
// below refuses when it is false.
OSV_MODULE_API bool available();

// The discovered capabilities (all zero when !available()).
OSV_MODULE_API const capabilities& caps();

// Program "ev" onto the current CPU and zero its count, leaving the other
// counters running.  Returns false if the PMU is unavailable or the event has
// no encoding for this family.  Call read() from the SAME CPU (pin the thread).
//
// The two fixed events (instructions_retired, unhalted_cycles) use separate
// fixed counters, so they and one programmable event can be counted at once.
// All programmable events share general counter 0: programming one replaces
// the previous one and invalidates its count.
OSV_MODULE_API bool program(event ev);

// Read the current count of "ev" on the current CPU.  Undefined if program(ev)
// returned false, another programmable event was programmed since, or the
// thread migrated since program(ev).
OSV_MODULE_API u64 read(event ev);

// Human-readable name of an event (for logging).
OSV_MODULE_API const char* name(event ev);

// RAII "count this region" helper.  Programs ev on construction and captures
// the delta on read()/destruction.  Keep the scope on one CPU (pin the thread)
// or the delta is meaningless.  ok() is false when the PMU could not arm.
class counter {
public:
    explicit counter(event ev) : _ev(ev) {
        _ok = program(ev);
        _start = _ok ? pmu::read(ev) : 0;
    }
    // Count accumulated since construction (or last reset()).
    u64 read() const { return _ok ? (pmu::read(_ev) - _start) : 0; }
    void reset() { _start = _ok ? pmu::read(_ev) : 0; }
    bool ok() const { return _ok; }
    event which() const { return _ev; }
private:
    event _ev;
    bool  _ok;
    u64   _start;
};

}

#endif /* OSV_PMU_HH */
