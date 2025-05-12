/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 * Copyright (C) 2015 Huawei Technologies Duesseldorf GmbH
 * Copyright (C) 2025 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/msi.hh>
#include <osv/sched.hh>
#include "gic-common.hh"

using namespace pci;

void msix_vector::set_affinity(sched::cpu *cpu)
{
    u64 msix_address;
    u32 msix_data;

    gic::gic->msi_format(&msix_address, &msix_data, _vector);
    for (auto entry_id : _entryids) {
        _dev->msix_write_entry(entry_id, msix_address, msix_data);
    }

    gic::gic->map_msi_vector(_vector, _dev, cpu->id);
}

interrupt_manager::interrupt_manager(pci::function* dev)
    : _dev(dev)
{
    gic::gic->allocate_msi_dev_mapping(dev);
}

interrupt_manager::~interrupt_manager()
{
}

bool interrupt_manager::setup_entry(unsigned entry_id, msix_vector* msix)
{
    auto vector = msix->get_vector();

    gic::gic->map_msi_vector(vector, _dev, 0);

    u64 msix_address;
    u32 msix_data;

    gic::gic->msi_format(&msix_address, &msix_data, vector);

    if (msix_address == 0) {
        return (false);
    }

    if (_dev->is_msix()) {
        if (!_dev->msix_write_entry(entry_id, msix_address, msix_data)) {
            return false;
        }
    } else {
        if (!_dev->msi_write_entry(entry_id, msix_address, msix_data)) {
            return false;
        }
    }

    msix->add_entryid(entry_id);
    return (true);
}
