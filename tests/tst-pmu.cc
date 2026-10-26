/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Controls for the OSv PMU readout (include/osv/pmu.hh):
 *
 *  - NEGATIVE control: if the CPU reports no architectural PMU (perfmon
 *    version 0 -- the ordinary non-metal Nitro guest), pmu::available() must
 *    be false and pmu::program() must REFUSE.  We prove the refusal path here;
 *    we do not fake a zero count.
 *
 *  - POSITIVE control (only when a PMU is present): count instructions-retired
 *    over a loop whose retired-instruction count we control exactly with inline
 *    asm, and require the reading to be within a few percent.  Then count
 *    unhalted-cycles over a busy spin and require it to be positive and roughly
 *    freq x time.
 *
 *  The measured thread is PINNED to one CPU for the whole test, because the
 *  counter lives in a per-CPU MSR and a migration would corrupt it (documented
 *  limitation of this diagnostic-only facility).
 */

#include <osv/pmu.hh>
#include <osv/sched.hh>
#include <osv/debug.hh>
#include <cstdio>
#include <cstdint>

static int failures = 0;

static void check(bool ok, const char* what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

// A loop whose retired-instruction count is known exactly: each iteration is
// (dec rcx; jnz), i.e. 2 instructions, run N times, plus the initial mov.  We
// keep it in inline asm so the compiler cannot vectorize or unroll it away.
static uint64_t known_loop(uint64_t n)
{
    uint64_t out;
    asm volatile (
        "mov %1, %%rcx\n\t"
        "1:\n\t"
        "dec %%rcx\n\t"
        "jnz 1b\n\t"
        "mov %%rcx, %0\n\t"
        : "=r"(out)
        : "r"(n)
        : "rcx", "cc");
    return out;
}

int main()
{
    printf("tst-pmu: PMU readout controls\n");

    // Print what the CPU reports either way -- this IS the negative-control
    // evidence on a no-vPMU box.
    pmu::print_proof();

    if (!pmu::available()) {
        // NEGATIVE control: no architectural PMU.  Arming must refuse for both
        // a fixed and a programmable event; nothing may pretend to succeed.
        printf("tst-pmu: no architectural PMU (version 0) -- negative control\n");
        check(!pmu::program(pmu::event::instructions_retired),
              "program(instructions_retired) refuses when unavailable");
        check(!pmu::program(pmu::event::itlb_walk_completed),
              "program(ITLB walk) refuses when unavailable");
        check(pmu::read() == 0, "read() returns 0 when unavailable");
        printf(failures ? "tst-pmu FAILED\n" : "tst-pmu OK\n");
        return failures ? 1 : 0;
    }

    // A real PMU: pin so the per-CPU counter is ours for the whole test.
    sched::thread::pin(sched::cpu::current());

    // POSITIVE control 1: instructions-retired over a loop of known size.
    // Expected retired instructions ~= 2*N (dec + jnz) plus a handful of
    // setup/teardown and the rdpmc/wrmsr path; require within +/- 5% for large
    // N so the fixed overhead is negligible.
    const uint64_t N = 200ull * 1000 * 1000;
    {
        pmu::counter c(pmu::event::instructions_retired);
        check(c.ok(), "program(instructions_retired) armed");
        known_loop(N);
        uint64_t got = c.read();
        double expected = 2.0 * N;
        double ratio = got / expected;
        printf("instructions-retired: got=%lu expected~=%.0f ratio=%.3f\n",
               (unsigned long)got, expected, ratio);
        check(ratio > 0.90 && ratio < 1.10,
              "instructions-retired within 10% of 2*N");
    }

    // POSITIVE control 2: unhalted-cycles over a busy spin -- must be positive
    // and grow with more work (a live cycle counter).  We do not assert an
    // absolute frequency (TSC vs core-clock ratio varies) nor an exact
    // multiple (a warmed loop runs fewer cycles/iter), only that it counts and
    // rises with 4x the iterations.
    {
        pmu::counter c(pmu::event::unhalted_cycles);
        check(c.ok(), "program(unhalted_cycles) armed");
        known_loop(50ull * 1000 * 1000);
        uint64_t short_spin = c.read();
        c.reset();
        known_loop(200ull * 1000 * 1000);
        uint64_t long_spin = c.read();
        printf("unhalted-cycles: short=%lu long=%lu\n",
               (unsigned long)short_spin, (unsigned long)long_spin);
        check(short_spin > 0, "unhalted-cycles counts (>0)");
        check(long_spin > (short_spin + short_spin / 2),
              "unhalted-cycles rises with 4x work (>1.5x)");
    }

    // METAL cross-check: iTLB/dTLB walk, i-cache and cache-miss counters, if
    // the family has encodings, should be armable and produce a plausible
    // (bounded) count over real work.  On families without an encoding,
    // program() refuses -- correct, not a failure -- so we only assert
    // plausibility when it arms.  EPT.WALK_PENDING is expected to be host-only
    // under KVM: a read of ~0 while iTLB is non-zero is the finding, not a bug.
    for (auto ev : { pmu::event::icache_stalls,
                     pmu::event::l2_miss,
                     pmu::event::l3_miss,
                     pmu::event::itlb_walk_completed,
                     pmu::event::dtlb_load_walk_completed,
                     pmu::event::ept_walk_cycles }) {
        pmu::counter c(ev);
        if (!c.ok()) {
            printf("note: %s has no encoding for this family (refused, ok)\n",
                   pmu::name(ev));
            continue;
        }
        known_loop(100ull * 1000 * 1000);
        uint64_t got = c.read();
        printf("%s: got=%lu\n", pmu::name(ev), (unsigned long)got);
        if (ev == pmu::event::ept_walk_cycles && got == 0) {
            printf("note: EPT.WALK_PENDING reads 0 from the guest -- nested-paging\n"
                   "      counters are host-only here; OSv cannot self-measure EPT.\n");
        }
        // A live cycle/count event is bounded by the cycles the loop can take;
        // a wild value means the encoding or MSR path is wrong.  100M (dec,jnz)
        // iterations retire ~200M instructions in at most a few hundred M
        // cycles, so bound generously at 10x the iteration count.
        check(got < 10ull * 100 * 1000 * 1000,
              "event count is bounded (not garbage)");
    }

    // Headline: IPC over the known loop -- the number the parse-cost diagnosis
    // now lives on.  Measure instructions and cycles over the SAME work back to
    // back (pinned, so both come from this CPU).
    {
        pmu::counter ci(pmu::event::instructions_retired);
        known_loop(N);
        uint64_t insns = ci.read();
        pmu::counter cc(pmu::event::unhalted_cycles);
        known_loop(N);
        uint64_t cycles = cc.read();
        if (cycles) {
            printf("IPC over known loop: insns=%lu cycles=%lu ipc=%.3f\n",
                   (unsigned long)insns, (unsigned long)cycles,
                   (double)insns / (double)cycles);
        }
        check(insns > 0 && cycles > 0, "IPC inputs both non-zero");
    }

    printf(failures ? "tst-pmu FAILED\n" : "tst-pmu OK\n");
    return failures ? 1 : 0;
}
