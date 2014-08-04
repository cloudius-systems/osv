/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "cpuid.hh"
#include "processor.hh"

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
};

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

    if (!(pfr & (0x0f << 16))) {
        cpuid_str += std::string(hwcap_str[HWCAP_BIT_FP]) + std::string(" ");
    }

    if (!(pfr & (0x0f << 20))) {
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

    if (((isar >> 8) & 0x0f) == 1) {
        cpuid_str += std::string(hwcap_str[HWCAP_BIT_SHA1]) + std::string(" ");
    }

    if (((isar >> 12) & 0x0f) == 1) {
        cpuid_str += std::string(hwcap_str[HWCAP_BIT_SHA2]) + std::string(" ");
    }

    if (((isar >> 16) & 0x0f) == 1) {
        cpuid_str += std::string(hwcap_str[HWCAP_BIT_CRC32]) + std::string(" ");
    }

    // we support of course the AArch64 instruction set. Adding
    // this at the end simplifies things like the .size() check above
    // and avoids ending with a space, while also conveying a more
    // or less obvious piece of information.
    cpuid_str += "a64";

    return cpuid_str;
}

}
