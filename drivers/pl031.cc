/*
 * Copyright (C) 2021 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "pl031.hh"
#include <osv/mmu.hh>

/* spec: see PrimeCell Real Time Clock (PL031) Technical Reference Manual.
 * implemented according to Revision: r1p3.
 * See https://static.docs.arm.com/ddi0224/c/real_time_clock_pl031_r1p3_technical_reference_manual_DDI0224C.pdf
 * Please note that this a minimal implementation of a subset of the
 * functionality which is enough to read the current time in seconds
 * at the boot time.
 */

#define rtc_reg(ADDR,REG_OFFSET) (*(volatile u32 *)(ADDR+REG_OFFSET))

pl031::pl031(u64 address)
{
    _address = address;
    mmu::linear_map((void *)_address, _address, mmu::page_size, mmu::page_size,
                    mmu::mattr::dev);
}

pl031::~pl031()
{
    mmu::munmap((void*)_address, mmu::page_size);
}

uint64_t pl031::wallclock_ns()
{
    // Let us read various identificatin registers to
    // verify that it is indeed a valid PL031 device
    if( rtc_reg(_address, RTCPeriphID0) == RTCPeriphID0_val &&
        rtc_reg(_address, RTCPeriphID1) == RTCPeriphID1_val &&
       (rtc_reg(_address, RTCPeriphID2) & RTCPeriphID2_mask) == RTCPeriphID2_val &&
        rtc_reg(_address, RTCPeriphID3) == RTCPeriphID3_val &&
        rtc_reg(_address, RTCPCellID0) == RTCPCellID0_val &&
        rtc_reg(_address, RTCPCellID1) == RTCPCellID1_val &&
        rtc_reg(_address, RTCPCellID2) == RTCPCellID2_val &&
        rtc_reg(_address, RTCPCellID3) == RTCPCellID3_val) {
        // Read value of RTC which is number of seconds since epoch
        // representing current time
	u64 epoch_time_in_seconds = rtc_reg(_address, RTCDR);
#if CONF_logger_debug
        debug_early_u64("pl031::wallclock_ns(): RTC seconds since epoch read as ", epoch_time_in_seconds);
#endif
	return epoch_time_in_seconds * 1000000000;
    } else {
	debug_early("pl031: could no detect!");
	return 0;
    }
}
