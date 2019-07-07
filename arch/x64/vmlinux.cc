/*
 * Copyright (C) 2019 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/types.h>
#include "arch-setup.hh"
#include "processor.hh"

#define OSV_MULTI_BOOT_INFO_ADDR      0x1000
#define OSV_E820_TABLE_ADDR           0x2000

//
// Instead of defining full boot_params and setup_header structs as in
// Linux source code, we define only handful of offsets pointing the fields
// we need to read from there. For details please this chunk of Linux code -
// https://github.com/torvalds/linux/blob/b6839ef26e549de68c10359d45163b0cfb031183/arch/x86/include/uapi/asm/bootparam.h#L151-L198
#define LINUX_KERNEL_BOOT_FLAG_MAGIC  0xaa55
#define LINUX_KERNEL_HDR_MAGIC        0x53726448 // "HdrS"

#define SETUP_HEADER_OFFSET  0x1f1   // look at bootparam.h in linux
#define SETUP_HEADER_FIELD_VAL(boot_params, offset, field_type) \
    (*static_cast<field_type*>(boot_params + SETUP_HEADER_OFFSET + offset))

#define BOOT_FLAG_OFFSET     sizeof(u8) + 4 * sizeof(u16) + sizeof(u32)
#define HDR_MAGIC_OFFSET     sizeof(u8) + 6 * sizeof(u16) + sizeof(u32)

#define E820_ENTRIES_OFFSET  0x1e8   // look at bootparam.h in linux
#define E820_TABLE_OFFSET    0x2d0   // look at bootparam.h in linux

#define CMD_LINE_PTR_OFFSET  sizeof(u8) * 5 + sizeof(u16) * 11 + sizeof(u32) * 7

struct linux_e820ent {
    u64 addr;
    u64 size;
    u32 type;
} __attribute__((packed));

// When OSv kernel gets booted directly as 64-bit ELF (loader.elf) as it is
// the case on firecracker we need a way to extract all necessary information
// about available memory and command line. This information is provided
// the struct boot_params (see details above) placed in memory at the address
// specified in the RSI register.
// The following extract_linux_boot_params() function is called from
// entry64 in boot.S and verifies OSV was indeed boot as Linux and
// copies memory and cmdline information into OSv multiboot struct.
// Please see https://www.kernel.org/doc/Documentation/x86/boot.txt for details
// of Linux boot protocol. Bear in mind that OSv implements very narrow specific
// subset of the protocol as assumed by firecracker.
extern "C" void extract_linux_boot_params(void *boot_params)
{   //
    // Verify we are being booted as Linux 64-bit ELF kernel
    assert( SETUP_HEADER_FIELD_VAL(boot_params, BOOT_FLAG_OFFSET, u16) == LINUX_KERNEL_BOOT_FLAG_MAGIC);
    assert( SETUP_HEADER_FIELD_VAL(boot_params, HDR_MAGIC_OFFSET, u32) == LINUX_KERNEL_HDR_MAGIC);

    // Set location of multiboot info struct at arbitrary place in lower memory
    // to copy to (happens to be the same as in boot16.S)
    osv_multiboot_info_type* mb_info = reinterpret_cast<osv_multiboot_info_type*>(OSV_MULTI_BOOT_INFO_ADDR);

    // Copy command line pointer from boot params
    mb_info->mb.cmdline = SETUP_HEADER_FIELD_VAL(boot_params, CMD_LINE_PTR_OFFSET, u32);

    // Copy e820 information from boot params
    mb_info->mb.mmap_length = 0;
    mb_info->mb.mmap_addr = OSV_E820_TABLE_ADDR;

    struct linux_e820ent *source_e820_table = static_cast<struct linux_e820ent *>(boot_params + E820_TABLE_OFFSET);
    struct e820ent *dest_e820_table = reinterpret_cast<struct e820ent *>(mb_info->mb.mmap_addr);

    u8 en820_entries = *static_cast<u8*>(boot_params + E820_ENTRIES_OFFSET);
    for (int e820_index = 0; e820_index < en820_entries; e820_index++) {
        dest_e820_table[e820_index].ent_size = 20;
        dest_e820_table[e820_index].type = source_e820_table[e820_index].type;
        dest_e820_table[e820_index].addr = source_e820_table[e820_index].addr;
        dest_e820_table[e820_index].size = source_e820_table[e820_index].size;
        mb_info->mb.mmap_length += sizeof(e820ent);
    }

    reset_bootchart(mb_info);
}
