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
#include <osv/debug.hh>

#include <sys/selinfo.h>
#include <sys/random.h>
#include <sys/param.h>

#include <dev/random/randomdev.h>
#include <dev/random/randomdev_soft.h>
#include <dev/random/random_adaptors.h>

extern "C" {
    void live_entropy_source_register(struct random_hardware_source *);
    void live_entropy_source_deregister(struct random_hardware_source *);
};

namespace randomdev {

struct random_device_priv {
    random_device* drv;
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

    // Blocking logic
    if (!random_adaptor->seeded) {
        error = (*random_adaptor->block)(ioflags);
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
// VIRTIO-RNG: hardware source of entropy.
//
static hw_rng* s_hwrng;
static int virtio_rng_read(void *buf, int size);

static struct random_hardware_source vrng = {
    "virtio-rng",
    RANDOM_PURE_VIRTIO,
    &virtio_rng_read,
};

// NOTE: This function is not intended to be called directly.
// Instead, it's registered as a callback into the structure used to register
// virtio-rng as a hardware source of entropy, so being called whenever needed.
static int virtio_rng_read(void *buf, int size)
{
    return s_hwrng->get_random_bytes(static_cast<char *>(buf), size);
}

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

random_device::random_device()
{
    struct random_device_priv *prv;

    if (s_hwrng) {
        live_entropy_source_register(&vrng);
    }
#ifdef __x86_64__
    if (processor::features().rdrand) {
        live_entropy_source_register(&drng);
    }
#endif
    (random_adaptor->init)();

    // Create random
    _random_dev = device_create(&random_device_driver, "random", D_CHR);
    prv = to_priv(_random_dev);
    prv->drv = this;

    // Create urandom as a sort of alias to random
    _urandom_dev = device_create(&random_device_driver, "urandom", D_CHR);
    prv = to_priv(_urandom_dev);
    prv->drv = this;
}

random_device::~random_device()
{
    if (s_hwrng) {
        live_entropy_source_deregister(&vrng);
    }
#ifdef __x86_64__
    if (processor::features().rdrand) {
        live_entropy_source_deregister(&drng);
    }
#endif
    (random_adaptor->deinit)();

    device_destroy(_random_dev);
    device_destroy(_urandom_dev);
}

void random_device::register_source(hw_rng* hwrng)
{
    s_hwrng = hwrng;
}

void randomdev_init()
{
    if (s_hwrng) {
        new random_device();
        debug("random: <%s> initialized\n", random_adaptor->ident);
    }
}

}
