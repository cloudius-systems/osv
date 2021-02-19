/*
 * Copyright (C) 2015 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "psci.hh"

#include <stdio.h>
#include <string.h>
#include <osv/debug.hh>
#include "arch-dtb.hh"

namespace psci {

psci _psci;

int (*psci::invoke_method)(u64, u64, u64, u64) = NULL;

#define asmeq(x, y)  ".ifnc " x "," y " ; .err ; .endif\n\t"
static __attribute__ ((noinline)) int invoke_hvc(u64 fid, u64 arg0, u64 arg1, u64 arg2)
{
    asm volatile(asmeq("%0", "x0")
                 asmeq("%1", "x1")
                 asmeq("%2", "x2")
                 asmeq("%3", "x3")
                 "hvc   #0\n"
                 : "+r" (fid)
                 : "r" (arg0), "r" (arg1), "r" (arg2));
    return fid;
}

static __attribute__ ((noinline)) int invoke_smc(u64 fid, u64 arg0, u64 arg1, u64 arg2)
{
    asm volatile(asmeq("%0", "x0")
                 asmeq("%1", "x1")
                 asmeq("%2", "x2")
                 asmeq("%3", "x3")
                 "smc   #0\n"
                 : "+r" (fid)
                 : "r" (arg0), "r" (arg1), "r" (arg2));
    return fid;
}
#undef asmeq

/* _psci object initialization */
/* __attribute__((constructor(init_prio::psci))) */
void psci::init()
{
    const char * method = dtb_get_psci_method();
    if (strcmp("hvc", method) == 0) {
        psci::invoke_method = invoke_hvc;
    } else if (strcmp("smc", method) == 0) {
        psci::invoke_method = invoke_smc;
    } else {
        abort("No PSCI method found");
    }

    int ret = _psci.psci_version();
    if (ret < 0) {
        abort("PSCI: failed invocation of PSCI_VERSION, missing PSCI?\n");
    }

    _psci.version.major = ret & ~0xffff;
    _psci.version.minor = ret & 0xffff;

    debugf("PSCI: version %u.%u detected.\n",
            _psci.version.major, _psci.version.minor);
}

int psci::invoke(u64 fid, u64 arg0, u64 arg1, u64 arg2)
{
    if (!psci::invoke_method) {
        abort("No PSCI method found");
    }
    return psci::invoke_method(fid, arg0, arg1, arg2);
}

int psci::psci_to_errno(s32 err)
{
    switch (err) {
    case psci_ret_val::SUCCESS:
        return 0;
    case psci_ret_val::NOT_SUPPORTED:
        return -ENOTSUP;
    case psci_ret_val::INVALID_PARAMS:
        return -EINVAL;
    case psci_ret_val::DENIED:
        return -EPERM;
    case psci_ret_val::ALREADY_ON:
        return -EBUSY;
    case psci_ret_val::ON_PENDING:
        return -EINPROGRESS;
    case psci_ret_val::INTERNAL_FAILURE:
        return -EPROTO;
    case psci_ret_val::NOT_PRESENT:
        return -ENOMEDIUM;
    case psci_ret_val::DISABLED:
        return -EFAULT;
    default:
        abort("PSCI: unexpected error code %d\n", err);
    };
    return -EINVAL;
}

int psci::psci_version()
{
    return invoke(psci_func::VERSION);
}

int psci::cpu_on(u64 target_cpu, u64 entry_point)
{
    int err = invoke(psci_func::CPU_ON, target_cpu, entry_point, 0);
    return psci_to_errno(err);
}

int psci::cpu_off()
{
    int err = invoke(psci_func::CPU_OFF);
    return psci_to_errno(err);
}

int psci::system_reset()
{
    int err = invoke(psci_func::SYSTEM_RESET);
    return psci_to_errno(err);
}

int psci::system_off()
{
    int err = invoke(psci_func::SYSTEM_OFF);
    return psci_to_errno(err);
}

}  /* namespace psci */
