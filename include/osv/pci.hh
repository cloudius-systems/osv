/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef PCI_HH_
#define PCI_HH_

/* PCI interfaces in OSv source files
 *
 * drivers/pci-generic.hh:
 *     generic PCI definitions; implementation in drivers/pci-generic.cc .
 *
 * arch/common/pci-config.hh:
 *     config space interfaces which all architectures need to implement.
 *
 * arch/$(arch)/arch-pci.hh:
 *     additional arch-specific definitions that might be needed on each arch.
 *
 * arch/$(arch)/pci.cc:
 *     arch-specific implementations for the interfaces above.
 *
 * include/osv/pci.hh:
 *     includes all the header files above for convenience.
 */

/* PCI class implementations in OSv source files
 *
 * drivers/pci-function.hh, drivers/pci-function.cc:
 *     PCI function class as well as the PCI bar class.
 *
 * drivers/pci-bridge.cc, drivers/pci-bridge.hh:
 *     PCI bridge class as a subclass of pci function
 *
 * drivers/pci-device.cc, drivers/pci-device.hh:
 *     PCI device class as a subclass of pci function
 */

#include "drivers/pci-generic.hh"
#include "pci-config.hh"
#include "arch-pci.hh"

#endif /* PCI_HH_ */
