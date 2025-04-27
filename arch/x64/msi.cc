/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <stddef.h>
#include <osv/msi.hh>
#include <osv/sched.hh>
#include "apic.hh"

using namespace pci;
using namespace processor;

void msix_vector::set_affinity(sched::cpu *cpu)
{
    msi_message msix_msg = apic->compose_msix(_vector, cpu->arch.apic_id);
    for (auto entry_id : _entryids) {
        _dev->msix_write_entry(entry_id, msix_msg._addr, msix_msg._data);
    }
}

interrupt_manager::interrupt_manager(pci::function* dev)
    : _dev(dev)
{
}

interrupt_manager::~interrupt_manager()
{
}

bool interrupt_manager::setup_entry(unsigned entry_id, msix_vector* msix)
{
    auto vector = msix->get_vector();
    msi_message msix_msg = apic->compose_msix(vector, 0);

    if (msix_msg._addr == 0) {
        return (false);
    }

    if (_dev->is_msix()) {
        if (!_dev->msix_write_entry(entry_id, msix_msg._addr, msix_msg._data)) {
            return false;
        }
    } else {
        if (!_dev->msi_write_entry(entry_id, msix_msg._addr, msix_msg._data)) {
            return false;
        }
    }

    msix->add_entryid(entry_id);
    return (true);
}
