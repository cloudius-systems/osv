/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/drivers_config.h>
#include "cpuid.hh"
#include "processor.hh"
#if CONF_drivers_xen
#include "xen.hh"
#endif

namespace processor {

/* this is the "right" hwcap order, in case we need AT_HWCAP */
static const char *hwcap_str[HWCAP_BIT_N] = {
    [HWCAP_BIT_FP] = "fp",
    [HWCAP_BIT_ASIMD] = "asimd",
    [HWCAP_BIT_EVTSTRM] = "evtstrm", /* unused, as no AArch32 support */
    [HWCAP_BIT_AES] = "aes",
    [HWCAP_BIT_PMULL] = "pmull",
    [HWCAP_BIT_SHA1] = "sha1",
    [HWCAP_BIT_SHA2] = "sha2",
    [HWCAP_BIT_CRC32] = "crc32",
    [HWCAP_BIT_ATOMIC] = "atomics",
};

#define IS_FP_SET(PFR)     (!(PFR & (0x0f << 16)))
#define IS_ASIMD_SET(PFR)  (!(PFR & (0x0f << 20)))
#define IS_SHA1_SET(ISAR)  (((ISAR >> 8) & 0x0f) == 1)
#define IS_SHA2_SET(ISAR)  (((ISAR >> 12) & 0x0f) == 1)
#define IS_CRC32_SET(ISAR) (((ISAR >> 16) & 0x0f) == 1)
#define IS_ATOMIC_SET(ISAR) (((ISAR >> 20) & 0x0f) == 2)

const std::string& features_str()
{
    static std::string cpuid_str;
    if (cpuid_str.size()) {
        return cpuid_str;
    }

    u64 isar; /* Instruction Set Attribute Register 0 */
    u64 pfr; /* Processor Feature Register 0 */

    asm volatile ("mrs %0, ID_AA64PFR0_EL1" : "=r"(pfr));
    asm volatile ("mrs %0, ID_AA64ISAR0_EL1" : "=r"(isar));

    if (IS_FP_SET(pfr)) {
        cpuid_str += std::string(hwcap_str[HWCAP_BIT_FP]) + std::string(" ");
    }

    if (IS_ASIMD_SET(pfr)) {
        cpuid_str += std::string(hwcap_str[HWCAP_BIT_ASIMD]) + std::string(" ");
    }

    unsigned int nibble = (isar >> 4) & 0x0f;
    switch (nibble) {
    case 2:
        cpuid_str += std::string(hwcap_str[HWCAP_BIT_PMULL]) + std::string(" ");
        /* fallthrough: case 2 also implies case 1. */
    case 1:
        cpuid_str += std::string(hwcap_str[HWCAP_BIT_AES]) + std::string(" ");
        break;
    default:
        ;
    }

    if (IS_SHA1_SET(isar)) {
        cpuid_str += std::string(hwcap_str[HWCAP_BIT_SHA1]) + std::string(" ");
    }

    if (IS_SHA2_SET(isar)) {
        cpuid_str += std::string(hwcap_str[HWCAP_BIT_SHA2]) + std::string(" ");
    }

    if (IS_CRC32_SET(isar)) {
        cpuid_str += std::string(hwcap_str[HWCAP_BIT_CRC32]) + std::string(" ");
    }

    if (IS_ATOMIC_SET(isar)) {
        cpuid_str += std::string(hwcap_str[HWCAP_BIT_ATOMIC]) + std::string(" ");
    }

    // we support of course the AArch64 instruction set. Adding
    // this at the end simplifies things like the .size() check above
    // and avoids ending with a space, while also conveying a more
    // or less obvious piece of information.
    cpuid_str += "a64";

    return cpuid_str;
}

#define HWCAP_BIT_CPUID 11

const unsigned long hwcap32()
{
    static unsigned long hwcap32;
    if (hwcap32) {
        return hwcap32;
    }

    u64 isar; /* Instruction Set Attribute Register 0 */
    u64 pfr; /* Processor Feature Register 0 */

    asm volatile ("mrs %0, ID_AA64PFR0_EL1" : "=r"(pfr));
    asm volatile ("mrs %0, ID_AA64ISAR0_EL1" : "=r"(isar));

    hwcap32 |= IS_FP_SET(pfr) << HWCAP_BIT_FP;
    hwcap32 |= IS_ASIMD_SET(pfr) << HWCAP_BIT_ASIMD;

    unsigned int nibble = (isar >> 4) & 0x0f;
    switch (nibble) {
    case 2:
        hwcap32 |= 1 << HWCAP_BIT_PMULL;
        /* fallthrough: case 2 also implies case 1. */
    case 1:
        hwcap32 |= 1 << HWCAP_BIT_AES;
        break;
    default:
        ;
    }

    hwcap32 |= IS_SHA1_SET(isar) << HWCAP_BIT_SHA1;
    hwcap32 |= IS_SHA2_SET(isar) << HWCAP_BIT_SHA2;
    hwcap32 |= IS_CRC32_SET(isar) << HWCAP_BIT_CRC32;
    hwcap32 |= IS_ATOMIC_SET(isar) << HWCAP_BIT_ATOMIC;

    hwcap32 |= 1 << HWCAP_BIT_CPUID;

    return hwcap32;
}

void process_cpuid(features_type& features)
{
#if CONF_drivers_xen
    xen::get_features(features);
#endif
}

const features_type& features()
{
    // features() can be used very early, make sure it is initialized
    static features_type f;
    return f;
}

features_type::features_type()
{
    process_cpuid(*this);
}

}
