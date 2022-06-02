/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "dmi.hh"

#include <osv/debug.hh>
#include <osv/mmu.hh>
#include <string.h>

std::string dmi_bios_vendor("Unknown");

static inline u8 read_u8(const char* buf, unsigned long idx)
{
    return buf[idx];
}

static inline u16 read_u16(const char* buf, unsigned long idx)
{
    return *reinterpret_cast<const u16*>(buf + idx);
}

static inline u32 read_u32(const char* buf, unsigned long idx)
{
    return *reinterpret_cast<const u32*>(buf + idx);
}

struct dmi_header {
    u8          type;
    u8          length;
    u16         handle;
    const char* data;
};

static const char* dmi_string(dmi_header header, u8 idx)
{
    if (!idx) {
        return "Not Specified";
    }
    auto s = header.data + header.length;
    while (idx > 1 && s) {
        s += strlen(s);
        s++;
        idx--;
    }
    return s;
}

static void dmi_table(u32 base, u16 len, u16 num)
{
    u8* const table_virt = mmu::phys_cast<u8>(base);

    mmu::linear_map(static_cast<void*>(table_virt), base, len, "smbios");

    auto start = reinterpret_cast<const char*>(table_virt);

    auto p = start;

    auto idx = 0;

    while (idx++ < num && p+4 < start+len) {
        dmi_header header;
        header.type   = read_u8 (p, 0x00);
        header.length = read_u8 (p, 0x01);
        header.handle = read_u16(p, 0x02);
        header.data   = p;

        if (header.length < 4) {
            debug_ll("DMI: error\n");
            break;
        }
        if (header.type == 127) {
            /* End-of-table */
            break;
        }
        switch (header.type) {
        case 0: /* 7.1. BIOS Information */
            if (header.length >= 18) {
                dmi_bios_vendor = dmi_string(header, header.data[0x04]);
            }
            break;
        default:
            break;
        }
        p += header.length;
        do {
            auto left = start + len - p;
            p = static_cast<char*>(memchr(p, '\0', left));
            if (!p) {
                debug_ll("DMI: error\n");
                return;
            }
            p++;
        } while (*p++);
    }
}

static u8 smbios_checksum(const char* p, size_t size)
{
    u8 sum = 0;
    for (size_t i = 0; i < size; i++) {
        sum += p[i];
    }
    return sum;
}

static void smbios_decode(const char* p)
{
    auto entry_len = read_u8(p, 0x05);
    // Entry point checksum:
    if (smbios_checksum(p, entry_len) != 0) {
        return;
    }
    // Intermediate anchor string:
    if (memcmp(p + 0x10, "_DMI_", 5) != 0) {
        return;
    }
    auto base = read_u32(p, 0x18);
    auto len  = read_u16(p, 0x16);
    auto num  = read_u16(p, 0x1c);

    dmi_table(base, len, num);
}

void dmi_probe()
{
    constexpr mmu::phys dmi_base = 0xf0000;

    u8* const dmi_virt = mmu::phys_cast<u8>(dmi_base);

    mmu::linear_map(static_cast<void*>(dmi_virt), dmi_base, 0x10000, "dmi");

    auto start = reinterpret_cast<const char*>(dmi_virt);

    for (auto off = 0; off <= 0xfff0; off += 16) {
        if (!memcmp(start+off, "_SM_", 4) && off <= 0xffe0) {
            smbios_decode(start+off);
        }
    }
}
