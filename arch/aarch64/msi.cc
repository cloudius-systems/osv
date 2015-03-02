/*
 * Copyright (C) 2015 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/* skeleton of MSI, not functional. */

#include <osv/msi.hh>

msix_vector::msix_vector(pci::function *dev) {}
msix_vector::~msix_vector() {}
pci::function *msix_vector::get_pci_function(void) { return nullptr; }
unsigned msix_vector::get_vector(void) { return 0; }
void msix_vector::msix_unmask_entries(void) {}
void msix_vector::msix_mask_entries(void) {}
void msix_vector::add_entryid(unsigned entry_id) {}
void msix_vector::interrupt(void) {}
void msix_vector::set_handler(std::function<void ()> handler) {}
void msix_vector::set_affinity(unsigned apic_id) {}

interrupt_manager::interrupt_manager(pci::function *dev) {}
interrupt_manager::~interrupt_manager() {}

bool interrupt_manager::easy_register(std::initializer_list<msix_binding> b)
{
    return false;
}
void interrupt_manager::easy_unregister() {}

std::vector<msix_vector *> interrupt_manager::request_vectors(unsigned n) {
    return _easy_vectors;
}
void interrupt_manager::free_vectors(const std::vector<msix_vector *> &v) {}
bool interrupt_manager::assign_isr(msix_vector *, std::function<void ()> h)
{
    return false;
}
bool interrupt_manager::setup_entry(unsigned entry_id, msix_vector *vector)
{
    return false;
}
bool interrupt_manager::unmask_interrupts(const std::vector<msix_vector *> &v) {
    return false;
}
