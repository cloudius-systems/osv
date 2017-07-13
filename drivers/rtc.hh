/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_RTC_HH
#define OSV_RTC_HH

#include <osv/types.h>

class rtc {
public:
    rtc();
    uint64_t wallclock_ns();
private:
    bool _is_bcd;
    uint8_t cmos_read(uint8_t val);
    uint8_t cmos_read_date(uint8_t val);
};

#endif //OSV_RTC_HH
