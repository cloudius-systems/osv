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
#include <osv/clock.hh>
#include <atomic>
#include <osv/kernel_config_core_reseed_on_resume.h>

#include <dev/random/randomdev.h>
#include <dev/random/randomdev_soft.h>
#include <dev/random/random_adaptors.h>
#include <dev/random/random_harvestq.h>
#include <dev/random/live_entropy_sources.h>

#ifdef __x86_64__
#include "processor.hh"
#endif

namespace randomdev {

#if CONF_core_reseed_on_resume
void reseed_on_resume();
#endif

struct random_device_priv {
    random_device* drv;
};

static random_device_priv *to_priv(device *dev)
{
    return reinterpret_cast<random_device_priv*>(dev->private_data);
}

#if CONF_core_reseed_on_resume
// Best-effort read-path resume detection, complementing kvmclock's thread.
// A gap can also be ordinary idle time; short pauses or a restored clock can
// be missed, and concurrent/kernel readers need not wait for this rekey.
// This is not a first-post-resume-read or clone-uniqueness guarantee.
static std::atomic<u64> _last_read_uptime{0};
// Set true once randomdev_init() has brought the device (and the harvest ring)
// up, so reseed_on_resume() is a true no-op if a resume is somehow detected
// before that (e.g. with --norandom).
static std::atomic<bool> _reseed_ready{false};

static void reseed_if_resumed()
{
    u64 now = (u64)::clock::get()->uptime();
    u64 prev = _last_read_uptime.exchange(now, std::memory_order_relaxed);
    // Skip the very first read (prev == 0) and only act on a large forward jump.
    // 1.5s matches the kvmclock detector; the purpose of this read-path check is
    // faster detection of the same resume event, not a lower threshold.
    if (prev != 0 && now > prev && (now - prev) > 1500000000ULL) {
        reseed_on_resume();
    }
}
#endif

static int
random_read(struct device *dev, struct uio *uio, int ioflags)
{
    int c, error = 0;
    char random_buf[PAGE_SIZE];

#if CONF_core_reseed_on_resume
    reseed_if_resumed();
#endif

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

#ifdef __x86_64__
    if (processor::features().rdrand) {
        live_entropy_source_register(&drng);
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
}

random_device::~random_device()
{
#ifdef __x86_64__
    if (processor::features().rdrand) {
        live_entropy_source_deregister(&drng);
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
#if CONF_core_reseed_on_resume
    _reseed_ready.store(true, std::memory_order_release);
#endif
}

#if CONF_core_reseed_on_resume
// Mix predictable timing data after a suspected resume. Distinct material can
// separate already-seeded clones, but clocks may repeat across restores and
// this does not add entropy or recover security from a disclosed snapshot.
// With no real entropy source an unseeded device must remain blocked.
void reseed_on_resume()
{
    // No-op until the random device has actually been initialized. random_adaptor
    // is always non-null (it points at the static soft CSPRNG context), so that
    // alone is not enough: with --norandom, or if a resume were somehow detected
    // before randomdev_init() ran, the harvest ring would not exist yet and
    // random_harvestq_internal() would dereference it. _reseed_ready is set true
    // only at the end of randomdev_init(), after random_harvestq_init().
    if (!random_adaptor || !_reseed_ready.load(std::memory_order_acquire)) {
        return;
    }

    // Best-effort divergence material, not guaranteed unique or secret.
    struct {
        u64 wall_ns;
        u64 tsc;
        u64 uptime_ns;
    } seed;
    seed.wall_ns = (u64)::clock::get()->time();
    seed.uptime_ns = (u64)::clock::get()->uptime();
#ifdef __x86_64__
    seed.tsc = processor::rdtsc();
#else
    seed.tsc = seed.uptime_ns;
#endif

    // Give timing data zero entropy credit. The flush processes queued events
    // and polls live sources; Yarrow rekeys only if already seeded (possibly
    // by credited events in that flush). No hardware source is required.
    random_harvestq_internal(seed.tsc, &seed, sizeof(seed),
                             0, RANDOM_PURE_RDRAND);
    if (random_adaptor->reseed) {
        (random_adaptor->reseed)();
    }
}
#endif

}
