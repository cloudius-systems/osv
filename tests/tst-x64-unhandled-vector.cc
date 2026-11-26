/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// An interrupt that arrives on an IDT vector with no handler must still be
// acknowledged.  A local APIC that receives no EOI keeps the vector's in-service
// bit set and holds back every vector of the same or a lower priority class on
// that CPU, among them the scheduler's wakeup IPI and the APIC timer, so the CPU
// stops running anything but its current thread.  The test registers a vector
// above them, unregisters it again, sends itself that vector from a pinned
// thread, and then checks that the CPU still sleeps on a timer and still wakes
// for another CPU.

#include <atomic>
#include <cstdio>

#include <osv/sched.hh>
#include <osv/debug.hh>
#include <osv/elf.hh>
#include "apic.hh"
#include "exceptions.hh"

using namespace std::chrono_literals;

static int failures;

static void check(bool ok, const char* what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    failures += !ok;
}

// The highest vector the IDT will hand out; it ranks above the wakeup IPI and
// the timer, which were registered at boot and hold low vectors.
static unsigned free_high_vector()
{
    std::vector<unsigned> taken;
    unsigned v;
    while ((v = idt.register_handler([] {})) != 0) {
        taken.push_back(v);
    }
    for (auto t : taken) {
        idt.unregister_handler(t);
    }
    return taken.empty() ? 0 : taken.back();
}

int main()
{
    if (sched::cpus.size() < 2) {
        printf("SKIP: needs 2 CPUs\n");
        return 0;
    }
    unsigned vector = free_high_vector();
    check(vector >= 0xe0, "found a free vector in the top priority class");

    std::atomic<bool> sent{false}, timer_ok{false}, woken{false};
    auto t = sched::thread::make([&] {
        processor::apic->self_ipi(vector);
        sent = true;
        // A timer on this CPU: with the in-service bit stuck, it never fires.
        sched::thread::sleep(10ms);
        timer_ok = true;
        // A wakeup from another CPU arrives through the wakeup IPI.
        sched::thread::wait_until([&] { return woken.load(); });
    }, sched::thread::attr().pin(sched::cpus[1]).name("tst-eoi"));
    t->start();

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!timer_ok && std::chrono::steady_clock::now() < deadline) {
        sched::thread::sleep(10ms);
    }
    check(sent, "the pinned thread sent the interrupt");
    check(timer_ok, "a timer still fires on the CPU that took it");
    if (!timer_ok) {
        printf("%d failures (CPU 1 is wedged; not joining)\n", failures);
        return 1;
    }

    t->wake_with([&] { woken = true; });
    deadline = std::chrono::steady_clock::now() + 5s;
    while (t->get_status() != sched::thread::status::terminated &&
           std::chrono::steady_clock::now() < deadline) {
        sched::thread::sleep(10ms);
    }
    check(t->get_status() == sched::thread::status::terminated,
          "a wakeup from another CPU still reaches it");
    if (failures == 0) {
        t->join();
        delete t;
    }

    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}

// The thread uses wait_until() and wake_with(), so this object must not cause
// sleepable lazy symbol resolutions or on-demand paging.
OSV_ELF_MLOCK_OBJECT();
