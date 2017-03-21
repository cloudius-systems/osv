/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef CPUID_HH_
#define CPUID_HH_

#include <string>

namespace processor {

extern const std::string& features_str();

/* bit positions would be compatible with Linux hwcap AT_HWCAP */
enum hwcap_bit {
    HWCAP_BIT_FP    = 0,
    HWCAP_BIT_ASIMD = 1,
    HWCAP_BIT_EVTSTRM = 2, /* unused, AArch32-compat only */
    HWCAP_BIT_AES = 3,
    HWCAP_BIT_PMULL = 4,
    HWCAP_BIT_SHA1 = 5,
    HWCAP_BIT_SHA2 = 6,
    HWCAP_BIT_CRC32 = 7,

    HWCAP_BIT_N
};

struct features_type {
    features_type();
    bool xen_clocksource;
    bool xen_vector_callback;
    bool xen_pci;
};

extern const features_type& features();

}

#endif /* CPUID_HH_ */
