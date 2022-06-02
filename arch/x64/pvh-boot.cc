#include "arch-setup.hh"
#include <xen/interface/arch-x86/hvm/start_info.h>

struct hvm_start_info* hvm_xen_start_info __attribute__((section(".data")));

#define OSV_MULTI_BOOT_INFO_ADDR      0x1000
#define OSV_E820_TABLE_ADDR           0x2000

extern "C"
void hvm_xen_extract_boot_params()
{
    // Set location of multiboot info struct at arbitrary place in lower memory
    // to copy to (happens to be the same as in boot16.S)
    osv_multiboot_info_type* mb_info = reinterpret_cast<osv_multiboot_info_type*>(OSV_MULTI_BOOT_INFO_ADDR);

    // Copy command line pointer from boot params
    mb_info->mb.cmdline = hvm_xen_start_info->cmdline_paddr;

    // Copy e820 information from boot params
    mb_info->mb.mmap_length = 0;
    mb_info->mb.mmap_addr = OSV_E820_TABLE_ADDR;

    struct hvm_memmap_table_entry *source_e820_table = reinterpret_cast<struct hvm_memmap_table_entry *>(hvm_xen_start_info->memmap_paddr);
    struct e820ent *dest_e820_table = reinterpret_cast<struct e820ent *>(mb_info->mb.mmap_addr);

    for (uint32_t e820_index = 0; e820_index < hvm_xen_start_info->memmap_entries; e820_index++) {
        dest_e820_table[e820_index].ent_size = 20;
        dest_e820_table[e820_index].type = source_e820_table[e820_index].type;
        dest_e820_table[e820_index].addr = source_e820_table[e820_index].addr;
        dest_e820_table[e820_index].size = source_e820_table[e820_index].size;
        mb_info->mb.mmap_length += sizeof(e820ent);
    }

    // Save ACPI RDSP address in the field of the osv_multiboot_info_type structure
    // Ideally, we would wanted to save it under the acpi::pvh_rsdp_paddr but it is
    // to early in the boot process as it would have been overwritten later in premain().
    mb_info->pvh_rsdp = hvm_xen_start_info->rsdp_paddr;

    reset_bootchart(mb_info);
}
