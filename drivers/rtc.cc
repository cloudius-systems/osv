/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <boost/date_time.hpp>
#include "rtc.hh"
#include "processor.hh"

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
