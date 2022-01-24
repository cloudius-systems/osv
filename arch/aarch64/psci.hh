/*
 * Copyright (C) 2015 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_PSCI_HH
#define ARCH_PSCI_HH

#include <osv/types.h>
#include <osv/prio.hh>

namespace psci {

enum psci_func : u64 {
    VERSION             = 0x84000000,
    CPU_SUSPEND         = 0xC4000001,
    CPU_OFF             = 0x84000002,
    CPU_ON              = 0xC4000003,
    AFFINITY_INFO       = 0xC4000004,
    MIGRATE             = 0xC4000005,
    MIGRATE_INFO_TYPE   = 0x84000006,
    MIGRATE_INFO_UP_CPU = 0xC4000007,
    SYSTEM_OFF          = 0x84000008,
    SYSTEM_RESET        = 0x84000009,
};

enum psci_ret_val : s32 {
    SUCCESS                     = 0,
    NOT_SUPPORTED               = -1,
    INVALID_PARAMS              = -2,
    DENIED                      = -3,
    ALREADY_ON                  = -4,
    ON_PENDING                  = -5,
    INTERNAL_FAILURE            = -6,
    NOT_PRESENT                 = -7,
    DISABLED                    = -8,
};

class psci {
public:
    __attribute__((constructor(init_prio::psci))) static void init();

    int cpu_on(u64 target_cpu, u64 entry_point); /* turn target cpu on */
    int cpu_off();                               /* turn current cpu off */
    int system_reset();                          /* reboot */
    int system_off();                            /* poweroff */

private:
    int psci_version();                          /* query supported PSCI ver */
    int psci_to_errno(s32 psci_err);
    static int __attribute__ ((noinline)) invoke(u64 fid, u64 arg0 = 0, u64 arg1 = 0, u64 arg2 = 0);
    static int (*invoke_method)(u64 fid, u64 arg0, u64 arg1, u64 arg2);

    struct {
        u32 major;
        u32 minor;
    } version;
};

extern psci _psci;

} /* namespace psci */

#endif
