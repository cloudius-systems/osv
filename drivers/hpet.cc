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
#include <boost/date_time.hpp>
#include <osv/prio.hh>
#include "processor.hh"
#include "clock.hh"
#include <osv/mmu.hh>
#include <osv/mmio.hh>
#include "arch.hh"
#include "xen.hh"
#include <osv/irqlock.hh>

using boost::intrusive::get_parent_from_member;

class hpetclock : public clock {
public:
    hpetclock(uint64_t hpet_address);
    virtual s64 time() __attribute__((no_instrument_function));
    virtual s64 uptime() override __attribute__((no_instrument_function));
    virtual s64 boot_time() override __attribute__((no_instrument_function));
private:
    mmioaddr_t _addr;
    uint64_t _wall;
    uint64_t _period;
};

class rtc {
public:
    rtc();
    uint64_t wallclock_ns();
private:
    bool _is_bcd;
    uint8_t cmos_read(uint8_t val);
    uint8_t cmos_read_date(uint8_t val);
};


#define RTC_PORT(x) (0x70 + (x))
#define RTC_BINARY_DATE 0x4

rtc::rtc()
{
    auto status = cmos_read(0xB);
    _is_bcd = !(status & RTC_BINARY_DATE);
}

uint8_t rtc::cmos_read(uint8_t addr)
{
    processor::outb(addr, RTC_PORT(0));
    return processor::inb(RTC_PORT(1));
}

uint8_t rtc::cmos_read_date(uint8_t addr)
{
    uint8_t val = cmos_read(addr);
    if (!_is_bcd)
        return val;
    return (val & 0x0f) + (val >> 4) * 10;
}

uint64_t rtc::wallclock_ns()
{
    // 0x80 : Update in progress. Wait for it.
    while ((cmos_read(0xA) & 0x80));

    uint8_t year = cmos_read_date(9);
    uint8_t month = cmos_read_date(8);
    uint8_t day = cmos_read_date(7);
    uint8_t hours = cmos_read_date(4);
    uint8_t mins = cmos_read_date(2);
    uint8_t secs = cmos_read_date(0);

    // FIXME: Get century from FADT.
    auto gdate = boost::gregorian::date(2000 + year, month, day);

    // My understanding from boost's documentation is that this handles leap
    // seconds correctly. They don't mention it explicitly, but they say that
    // one of the motivations for writing the library is that: " most libraries
    // do not correctly handle leap seconds, provide concepts such as infinity,
    // or provide the ability to use high resolution or network time sources"
    auto now = boost::posix_time::ptime(gdate, 
                    boost::posix_time::hours(hours) +
                    boost::posix_time::minutes(mins) +
                    boost::posix_time::seconds(secs));

    auto base = boost::posix_time::ptime(boost::gregorian::date(1970, 1, 1));
    auto dur = now - base;

    return dur.total_nanoseconds();
}

#define HPET_CAP        0x000
#define HPET_CAP_COUNT_SIZE (1<<13)
#define HPET_PERIOD     0x004
#define HPET_CONFIG     0x010
#define HPET_COUNTER    0x0f0

#define MAX_PERIOD     100000000UL
#define MIN_PERIOD     1000000UL

hpetclock::hpetclock(uint64_t hpet_address)
{
    // If we ever need another rtc user, it should be global. But
    // we should really, really avoid it. So let it local.
    auto r = new rtc();
    _addr = mmio_map(hpet_address, 4096);

    // Verify that a 64-bit counter is supported, and we're not forced to
    // operate in 32-bit mode (which has interrupt on every wrap-around).
    auto cap = mmio_getl(_addr + HPET_CAP);
    assert(cap & HPET_CAP_COUNT_SIZE);

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

s64 hpetclock::time()
{
    return _wall + (mmio_getq(_addr + HPET_COUNTER) * _period);
}

s64 hpetclock::uptime()
{
    return (mmio_getq(_addr + HPET_COUNTER) * _period);
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
        assert(st == AE_OK);
        auto h = get_parent_from_member(hpet_header, &ACPI_TABLE_HPET::Header);
        auto hpet_address = h->Address;

        clock::register_clock(new hpetclock(hpet_address.Address));
    }, {});
}
