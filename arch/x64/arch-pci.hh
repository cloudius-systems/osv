/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_PCI_HH
#define ARCH_PCI_HH

#include "processor.hh"

namespace pci {

using processor::inb;
using processor::inw;
using processor::inl;
using processor::outb;
using processor::outw;
using processor::outl;

} /* namespace pci */

#endif /* ARCH_PCI_HH */
