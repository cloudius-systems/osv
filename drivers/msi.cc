/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 * Copyright (C) 2025 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <stddef.h>
#include <osv/msi.hh>
#include <osv/trace.hh>

TRACEPOINT(trace_msix_interrupt, "vector=0x%02x", unsigned);
TRACEPOINT(trace_msix_migrate, "vector=0x%02x cpu_id=0x%x",
                               unsigned, unsigned);

using namespace pci;
using namespace processor;

msix_vector::msix_vector(pci::function* dev)
    : _dev(dev)
{
    _vector = idt.register_handler([this] { interrupt(); });
}

msix_vector::~msix_vector()
{
    idt.unregister_handler(_vector);
}

pci::function* msix_vector::get_pci_function(void)
{
    return (_dev);
}

unsigned msix_vector::get_vector(void)
{
    return (_vector);
}

void msix_vector::msix_unmask_entries(void)
{
    for (auto entry_id : _entryids) {
        if (_dev->is_msix()) {
            _dev->msix_unmask_entry(entry_id);
        } else {
            _dev->msi_unmask_entry(entry_id);
        }
    }
}

void msix_vector::msix_mask_entries(void)
{
    for (auto entry_id : _entryids) {
        if (_dev->is_msix()) {
            _dev->msix_mask_entry(entry_id);
        } else {
            _dev->msi_mask_entry(entry_id);
        }
    }
}

void msix_vector::set_handler(std::function<void ()> handler)
{
    _handler = handler;
}

void msix_vector::add_entryid(unsigned entry_id)
{
    _entryids.push_back(entry_id);
}

void msix_vector::interrupt(void)
{
    trace_msix_interrupt(_vector);
    _handler();
}

/**
 * Changes the affinity of the MSI-X vector to the same CPU where its service
 * routine thread is bound and then wakes that thread.
 *
 * @param current The CPU to which the MSI-X vector is currently bound
 * @param v MSI-X vector handle
 * @param t interrupt service routine thread
 */
static inline void set_affinity_and_wake(
    sched::cpu*& current, msix_vector* v, sched::thread* t)
{
    auto cpu = t->get_cpu();;

    if (cpu != current) {

        //
        // According to PCI spec chapter 6.8.3.5 the MSI-X table entry may be
        // updated only if the entry is masked and the new values are promissed
        // to be read only when the entry is unmasked.
        //
        v->msix_mask_entries();

        std::atomic_thread_fence(std::memory_order_seq_cst);

        current = cpu;
        trace_msix_migrate(v->get_vector(), cpu->id);
        v->set_affinity(cpu);

        std::atomic_thread_fence(std::memory_order_seq_cst);

        v->msix_unmask_entries();
    }

    t->wake_with_irq_disabled();
}

bool interrupt_manager::easy_register(std::initializer_list<msix_binding> bindings)
{
    unsigned n = bindings.size();
    debug("interrupt_manager::easy_register()\n");

    std::vector<msix_vector*> assigned = request_vectors(n);

    if (assigned.size() != n) {
        free_vectors(assigned);
        return (false);
    }

    // Enable the device msix capability,
    // masks all interrupts...

    if (_dev->is_msix()) {
        _dev->msix_enable();
    } else {
        _dev->msi_enable();
    }

    int idx=0;

    for (auto binding : bindings) {
        msix_vector* vec = assigned[idx++];
        auto isr = binding.isr;
        auto t = binding.t;
        debugf("interrupt_manager::easy_register(), idx:%d\n", idx);

        bool assign_ok;

        if (t) {
            sched::cpu* current = nullptr;
            assign_ok =
                assign_isr(vec,
                    [=]() mutable {
                                    if (isr)
                                        isr();
                                    set_affinity_and_wake(current, vec, t);
                                  });
        } else {
            assign_ok = assign_isr(vec, [=]() { if (isr) isr(); });
        }

        debugf("interrupt_manager::easy_register(), idx:%d, assign_ok:%d\n", idx, assign_ok);
        if (!assign_ok) {
            free_vectors(assigned);
            return false;
        }
        bool setup_ok = setup_entry(binding.entry, vec);
        if (!setup_ok) {
            free_vectors(assigned);
            return false;
        }
    }

    // Save reference for assigned vectors
    _easy_vectors = assigned;
    unmask_interrupts(assigned);

    return (true);
}

void interrupt_manager::easy_unregister()
{
    free_vectors(_easy_vectors);
    _easy_vectors.clear();
}

std::vector<msix_vector*> interrupt_manager::request_vectors(unsigned num_vectors)
{
    std::vector<msix_vector*> results;
    unsigned num_entries;

    if (_dev->is_msix()) {
        num_entries = _dev->msix_get_num_entries();
    } else {
        num_entries = _dev->msi_get_num_entries();
    }

    auto num = std::min(num_vectors, num_entries);

    for (unsigned i = 0; i < num; ++i) {
        results.push_back(new msix_vector(_dev));
    }

    return (results);
}

bool interrupt_manager::assign_isr(msix_vector* vector, std::function<void ()> handler)
{
    vector->set_handler(handler);

    return (true);
}

void interrupt_manager::free_vectors(const std::vector<msix_vector*>& vectors)
{
    for (auto msix : vectors) {
        delete msix;
    }
}

bool interrupt_manager::unmask_interrupts(const std::vector<msix_vector*>& vectors)
{
    for (auto msix : vectors) {
        msix->msix_unmask_entries();
    }

    return (true);
}
