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
    //TODO: Implement it
}

interrupt_manager::interrupt_manager(pci::function* dev)
    : _dev(dev)
{
    //TODO: Implement it
}

interrupt_manager::~interrupt_manager()
{
}

bool interrupt_manager::setup_entry(unsigned entry_id, msix_vector* msix)
{
    //TODO: Implement it
    return (true);
}
