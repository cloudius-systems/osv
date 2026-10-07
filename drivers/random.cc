/*-
 * Copyright (c) 2000-2013 Mark R V Murray
 * Copyright (c) 2013 Arthur Mesh <arthurmesh@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/random.hh"
#include <assert.h>

#include <osv/device.h>
#include <osv/uio.h>
#include <osv/vnode.h>
#include <fcntl.h>
#include <osv/debug.hh>

#include <dev/random/randomdev.h>
#include <dev/random/randomdev_soft.h>
#include <dev/random/random_adaptors.h>
#include <dev/random/live_entropy_sources.h>
#ifdef __aarch64__
#include "cpuid.hh"
#endif

namespace randomdev {

struct random_device_priv {
    random_device* drv;
    bool urandom;
};

static random_device_priv *to_priv(device *dev)
{
    return reinterpret_cast<random_device_priv*>(dev->private_data);
}

static int
random_read(struct device *dev, struct uio *uio, int ioflags)
{
    int c, error = 0;
    char random_buf[PAGE_SIZE];

    // Blocking logic.  VFS passes IO_* flags, and the adaptor's block() takes
    // open(2) flags.  As on Linux, /dev/urandom ignores O_NONBLOCK and never
    // fails with EAGAIN; unlike Linux, it still waits for the first seeding.
    if (!random_adaptor->seeded) {
        bool nonblock = (ioflags & IO_NONBLOCK) && !to_priv(dev)->urandom;
        error = (*random_adaptor->block)(nonblock ? O_NONBLOCK : 0);
    }

    if (!error) {
        while (uio->uio_resid > 0 && !error) {
            c = std::min(uio->uio_resid, static_cast<long int>(PAGE_SIZE));
            c = (*random_adaptor->read)(static_cast<void *>(random_buf), c);
            error = uiomove(random_buf, c, uio);
        }

        // Finished reading; let the source know so it can do some
        // optional housekeeping */
        (*random_adaptor->read)(nullptr, 0);
    }

    return error;
}

static int
random_write(struct device *dev, struct uio *uio, int ioflags)
{
    // We used to allow this to insert userland entropy.
    // We don't any more because (1) this so-called entropy
    // is usually lousy and (b) its vaguely possible to
    // mess with entropy harvesting by overdoing a write.
    // Now we just ignore input like /dev/null does.
    uio->uio_resid = 0;

    return 0;
}

static struct devops random_device_devops {
    no_open,
    no_close,
    random_read,
    random_write,
    no_ioctl,
    no_devctl,
};

struct driver random_device_driver = {
    "random",
    &random_device_devops,
    sizeof(struct random_device_priv),
};

//
// Intel DRNG, RDRAND: hardware source of entropy.
// Implementation based on the following Intel manual:
// Intel(r) Digital Random Number Generator (DRNG)
//
#ifdef __x86_64__
static int drng_read(void *, int);

// The constant below is based on the aforementioned Intel manual.
// It recommends that RDRAND users should retry 10 times when the
// instruction failed to work as expected.
static constexpr int rdrand_retries_max = 10;

static struct random_hardware_source drng = {
    "intel drng, rdrand",
    RANDOM_PURE_RDRAND,
    &drng_read,
};

static inline bool rdrand_with_retries(uint64_t *data)
{
    for (auto retry = 0; retry <= rdrand_retries_max; retry++) {
        if (processor::rdrand(data)) {
            return true;
        }
    }
    return false;
}

static int
drng_read(void *buf, int size)
{
    uint64_t *dest = static_cast<uint64_t *>(buf);
    uint64_t data;
    unsigned qwords, qwords_to_read;

    assert((size & (sizeof(uint64_t) -1)) == 0);
    qwords_to_read = size / sizeof(uint64_t);

    for (qwords = 0; qwords < qwords_to_read; qwords++) {
        if (!rdrand_with_retries(&data)) {
            // Handle unlikely case where RDRAND has failed after
            // all the retries.
            break;
        }

        *dest++ = data;
    }
    return qwords * sizeof(uint64_t);
}
#endif

//
// Arm FEAT_RNG, RNDR: architectural hardware source of entropy. RNDR returns
// a 64-bit random number with PSTATE.NZCV set to 0b0000 on success, or zero
// with NZCV 0b0100 (Z set) when no random number was available in a
// reasonable time.
//
#ifdef __aarch64__
static int rndr_read(void *, int);

// The same retry policy as for RDRAND above.
static constexpr int rndr_retries_max = 10;

static struct random_hardware_source arm_rndr = {
    "arm rndr",
    RANDOM_PURE_RDRAND,
    &rndr_read,
};

static inline bool rndr_with_retries(uint64_t *data)
{
    for (auto retry = 0; retry <= rndr_retries_max; retry++) {
        uint64_t val;
        uint64_t fail;
        // mrs <x>, RNDR (S3_3_C2_C4_0); Z is set if the read failed.
        asm volatile("mrs %0, s3_3_c2_c4_0\n\t"
                     "cset %1, eq\n\t"
                     : "=r"(val), "=r"(fail)
                     :
                     : "cc");
        if (!fail) {
            *data = val;
            return true;
        }
    }
    return false;
}

static int
rndr_read(void *buf, int size)
{
    uint64_t *dest = static_cast<uint64_t *>(buf);
    uint64_t data;
    unsigned qwords, qwords_to_read;

    assert((size & (sizeof(uint64_t) - 1)) == 0);
    qwords_to_read = size / sizeof(uint64_t);

    for (qwords = 0; qwords < qwords_to_read; qwords++) {
        if (!rndr_with_retries(&data)) {
            break;
        }

        *dest++ = data;
    }

    return qwords * sizeof(uint64_t);
}
#endif

random_device::random_device()
{
    struct random_device_priv *prv;

#ifdef __x86_64__
    if (processor::features().rdrand) {
        live_entropy_source_register(&drng);
    }
#endif
#ifdef __aarch64__
    if (processor::features().rndr) {
        live_entropy_source_register(&arm_rndr);
    }
#endif
    if (live_entropy_sources_empty()) {
        debug("Warning: No hardware source of entropy available to your "
            "platform,\n\tCSPRNG will rely on software source of entropy to "
            "provide high-quality randomness.\n");
    }
    (random_adaptor->init)();

    // Create random
    _random_dev = device_create(&random_device_driver, "random", D_CHR);
    prv = to_priv(_random_dev);
    prv->drv = this;

    // Create urandom as a sort of alias to random
    _urandom_dev = device_create(&random_device_driver, "urandom", D_CHR);
    prv = to_priv(_urandom_dev);
    prv->drv = this;
    prv->urandom = true;
}

random_device::~random_device()
{
#ifdef __x86_64__
    if (processor::features().rdrand) {
        live_entropy_source_deregister(&drng);
    }
#endif
#ifdef __aarch64__
    if (processor::features().rndr) {
        live_entropy_source_deregister(&arm_rndr);
    }
#endif
    (random_adaptor->deinit)();

    device_destroy(_random_dev);
    device_destroy(_urandom_dev);
}

void randomdev_init()
{
    new random_device();
    debugf("random: <%s> initialized\n", random_adaptor->ident);
}

}
