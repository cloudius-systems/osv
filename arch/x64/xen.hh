/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef XEN_ARCH_HH
#define XEN_ARCH_HH

#include "cpuid.hh"

class gsi_level_interrupt;

namespace xen {

void xen_init(processor::features_type &features, unsigned base);
gsi_level_interrupt *xen_set_callback(int irqno);

}

#endif
