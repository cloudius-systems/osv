/*
 * Copyright (C) 2017 Sergiy Kibrik <sergiy.kibrik@globallogic.com>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef XEN_ARCH_HH
#define XEN_ARCH_HH

#include "cpuid.hh"

namespace xen {

void get_features(processor::features_type &features);

}

#endif /* XEN_ARCH_HH */


