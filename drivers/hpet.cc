/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

extern "C" {
#include "acpi.h"
}
#include <boost/intrusive/parent_from_member.hpp>
#include <osv/prio.hh>
#include "processor.hh"
#include "clock.hh"
#include <osv/mmu.hh>
#include <osv/mmio.hh>
#include "arch.hh"
#include <osv/xen.hh>
#include <osv/irqlock.hh>
#include "rtc.hh"

using boost::intrusive::get_parent_from_member;

class hpetclock : public clock {
public:
    hpetclock(mmioaddr_t hpet_mmio_address);
    virtual s64 boot_time() override __attribute__((no_instrument_function));
protected:
    mmioaddr_t _addr;
    uint64_t _wall;
    uint64_t _period;
};

#define HPET_COUNTER    0x0f0

//FIXME: Enhance this class to handle wrap-around
class hpet_32bit_clock : public hpetclock {
public:
    hpet_32bit_clock(mmioaddr_t hpet_mmio_address) : hpetclock(hpet_mmio_address) {
        debug_early_u64("WARNING: hpet with 32-bit counter will wrap around in seconds: ",
            (_period * (1UL << 32)) / 1000000000UL);
    }
protected:
    virtual s64 time() override __attribute__((no_instrument_function)) {
        return _wall + mmio_getl(_addr + HPET_COUNTER) * _period;
    }

    virtual s64 uptime() override __attribute__((no_instrument_function)) {
        return mmio_getl(_addr + HPET_COUNTER) * _period;
    }
};

class hpet_64bit_clock : public hpetclock {
public:
    hpet_64bit_clock(mmioaddr_t hpet_mmio_address) : hpetclock(hpet_mmio_address) {}
protected:
    virtual s64 time() override __attribute__((no_instrument_function)) {
        return _wall + mmio_getq(_addr + HPET_COUNTER) * _period;;
    }

    virtual s64 uptime() override __attribute__((no_instrument_function)) {
        return mmio_getq(_addr + HPET_COUNTER) * _period;;
    }
};

#define HPET_CAP        0x000
#define HPET_CAP_COUNT_SIZE (1<<13)
#define HPET_PERIOD     0x004
#define HPET_CONFIG     0x010

#define MAX_PERIOD     100000000UL
#define MIN_PERIOD     1000000UL

hpetclock::hpetclock(mmioaddr_t hpet_mmio_address)
{
    _addr = hpet_mmio_address;
    // If we ever need another rtc user, it should be global. But
    // we should really, really avoid it. So let it local.
    auto r = new rtc();

    unsigned int cfg = mmio_getl(_addr + HPET_CONFIG);
    // Stop the HPET First, so we can make sure the counter is at 0 when we
    // start it. We will restart it as soon as we read the wallclock info from
    // the RTC
    cfg &= ~0x1;
    mmio_setl(_addr + HPET_CONFIG, cfg);

    _period = mmio_getl(_addr + HPET_PERIOD);
    // Hpet has its period presented in femtoseconds.
    assert((_period >= MIN_PERIOD) && (_period <= MAX_PERIOD));
    _period /= 1000000UL; // nanoseconds

    // In theory we should disable NMIs, but on virtual hardware, we can
    // relax that (This is specially true given our current NMI handler,
    // which will just halt us forever.
    irq_save_lock_type irq_lock;
    WITH_LOCK(irq_lock) {
        // Now we set it to 0
        mmio_setl(_addr + HPET_COUNTER, 0);
        mmio_setl(_addr + HPET_COUNTER + 4, 0);

        _wall = r->wallclock_ns();

        // We got them all, now we restart the HPET.
        cfg |= 0x1;
        mmio_setl(_addr + HPET_CONFIG, cfg);
    };
}

s64 hpetclock::boot_time()
{
    // The following is time()-uptime():
    return _wall;
}

void __attribute__((constructor(init_prio::hpet))) hpet_init()
{
    XENPV_ALTERNATIVE(
    {
        auto c = clock::get();

        // HPET should be only used as a fallback, if no other pvclocks
        // are present
        if (c != nullptr) {
            return;
        }

        char hpet_sig[] = ACPI_SIG_HPET;
        ACPI_TABLE_HEADER *hpet_header;
        auto st = AcpiGetTable(hpet_sig, 0, &hpet_header);
        // If we don't have a paravirtual clock, nor an HPET clock, we currently
        // have no chance of running.
        if (st != AE_OK) {
            abort("Neither paravirtual clock nor HPET is available.\n");
        }
        auto h = get_parent_from_member(hpet_header, &ACPI_TABLE_HPET::Header);
        auto hpet_address = h->Address;

        // Check what type of main counter - 32-bit or 64-bit - is available and
        // construct relevant hpet clock instance
        mmioaddr_t hpet_mmio_address = mmio_map(hpet_address.Address, 4096);

        auto cap = mmio_getl(hpet_mmio_address + HPET_CAP);
        if (cap & HPET_CAP_COUNT_SIZE) {
            clock::register_clock(new hpet_64bit_clock(hpet_mmio_address));
        }
        else {
            clock::register_clock(new hpet_32bit_clock(hpet_mmio_address));
        }
    }, {});
}
