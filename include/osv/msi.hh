/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef MSI_HH_
#define MSI_HH_

#include "drivers/pci-function.hh"

#include <list>

namespace sched {
struct cpu;
}

class msix_vector {
public:
    msix_vector(pci::function* dev);
    virtual ~msix_vector();

    pci::function* get_pci_function(void);
    unsigned get_vector(void);
    void msix_unmask_entries(void);
    void msix_mask_entries(void);

    void add_entryid(unsigned entry_id);
    void interrupt(void);
    void set_handler(std::function<void ()> handler);
    void set_affinity(sched::cpu *cpu);

private:
    // Handler to invoke...
    std::function<void ()> _handler;
    // The device that owns this vector
    pci::function * _dev;
    // Entry ids used by this vector
    std::list<unsigned> _entryids;
    unsigned _vector;
};

// entry -> thread to wake
struct msix_binding {
    unsigned entry;
    // high priority ISR
    std::function<void ()> isr;
    // bottom half
    sched::thread *t;
};

class interrupt_manager {
public:

    explicit interrupt_manager(pci::function* dev);
    ~interrupt_manager();

    ////////////////////
    // Easy Interface //
    ////////////////////

    // 1. Enabled MSI-x For device
    // 2. Allocate vectors and assign ISRs
    // 3. Setup entries
    // 4. Unmask interrupts
    bool easy_register(std::initializer_list<msix_binding> bindings);
    void easy_unregister();

    /////////////////////
    // Multi Interface //
    /////////////////////

    std::vector<msix_vector*> request_vectors(unsigned num_vectors);
    void free_vectors(const std::vector<msix_vector*>& vectors);
    bool assign_isr(msix_vector*, std::function<void ()> handler);
    // Multiple entry can be assigned the same vector
    bool setup_entry(unsigned entry_id, msix_vector* vector);
    // unmasks all interrupts
    bool unmask_interrupts(const std::vector<msix_vector*>& vectors);

private:
    pci::function* _dev;
    // Used by the easy interface
    std::vector<msix_vector*> _easy_vectors;
};

#endif /* MSI_HH_ */
