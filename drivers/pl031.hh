/*
 * Copyright (C) 2021 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_PL031_HH
#define OSV_PL031_HH

#include <osv/types.h>

#define RTCDR             0x000 /* data */
#define RTCMR             0x004 /* match */
#define RTCLR             0x008 /* load */
#define RTCCR             0x00c /* control */
#define RTCIMSC           0x010 /* interrupt mask set or clear */
#define RTCRIS            0x014 /* raw interrupt status */
#define RTCMIS            0x018 /* masked interrupt status */
#define RTCICR            0x01c /* interrupt clear */
#define RTCPeriphID0      0xfe0 /* peripheral ID bits [7:0] */
#define RTCPeriphID1      0xfe4 /* peripheral ID bits [15:8] */
#define RTCPeriphID2      0xfe8 /* peripheral ID bits [23:16] */
#define RTCPeriphID3      0xfec /* peripheral ID bits [31:24] */
#define RTCPCellID0       0xff0 /* PrimeCell ID bits [7:0] */
#define RTCPCellID1       0xff4 /* PrimeCell ID bits [7:0] */
#define RTCPCellID2       0xff8 /* PrimeCell ID bits [7:0] */
#define RTCPCellID3       0xffc /* PrimeCell ID bits [7:0] */

#define RTCPeriphID0_val  0x31
#define RTCPeriphID1_val  0x10
#define RTCPeriphID2_mask 0x0f
#define RTCPeriphID2_val  0x04
#define RTCPeriphID3_val  0x00

#define RTCPCellID0_val   0x0d
#define RTCPCellID1_val   0xf0
#define RTCPCellID2_val   0x05
#define RTCPCellID3_val   0xb1

class pl031 {
public:
    pl031(u64 address);
    ~pl031();
    uint64_t wallclock_ns();
private:
    u64 _address;
};

#endif //OSV_PL031_HH
