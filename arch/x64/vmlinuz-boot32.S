# Copyright (C) 2019 Waldemar Kozaczuk
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.

.code32
.data

loader = OSV_KERNEL_BASE
start32_from_vmlinuz = loader + 0x900
magic = 0xAA55
vmlinuz_load_address = OSV_KERNEL_BASE - 0x400
vmlinuz_size = OSV_KERNEL_SIZE + 0x400

.rept 0x1f1
.byte 0
.endr

setup_sects:           .byte  1                     /* The size of the setup in sectors - (-1 in our case)*/
root_flags:            .2byte 0                     /* If set, the root is mounted readonly */
syssize:               .4byte 0x400                 /* The size of the 32-bit code in 16-byte paras */
ram_size:              .2byte 0                     /* DO NOT USE - for bootsect.S use only */
vid_mode:              .2byte 0                     /* Video mode control */
root_dev:              .2byte 0                     /* Default root device number */
boot_flag:             .2byte magic                 /* 0xAA55 magic number */
jump:                  .2byte 0                     /* Jump instruction */
header:                .ascii "HdrS"                /* Magic signature "HdrS" */
version:               .2byte 0x020a                /* Boot protocol version supported */
realmode_swtch:        .4byte 0                     /* Boot loader hook (see below) */
start_sys_seg:         .2byte 0                     /* The load-low segment (0x1000) (obsolete) */
kernel_version:        .2byte 0                     /* Pointer to kernel version string */
type_of_loader:        .byte  0                     /* Boot loader identifier */
loadflags:             .byte  1                     /* Boot protocol option flags */
setup_move_size:       .2byte 0                     /* Move to high memory size (used with hooks) */
code32_start:          .4byte 0                     /* Boot loader hook (see below) */
ramdisk_image:         .4byte 0                     /* initrd load address (set by boot loader) */
ramdisk_size:          .4byte 0                     /* initrd size (set by boot loader) */
bootsect_kludge:       .4byte 0                     /* DO NOT USE - for bootsect.S use only */
heap_end_ptr:          .2byte 0                     /* Free memory after setup end */
ext_loader_ver:        .byte  0                     /* Extended boot loader version */
ext_loader_type:       .byte  0                     /* Extended boot loader ID */
cmd_line_ptr:          .4byte 0                     /* 32-bit pointer to the kernel command line */
initrd_addr_max:       .4byte 0                     /* Highest legal initrd address */
kernel_alignment:      .4byte 0                     /* Physical addr alignment required for kernel */
relocatable_kernel:    .byte  0                     /* Whether kernel is relocatable or not */
min_alignment:         .byte  0                     /* Minimum alignment, as a power of two */
xloadflags:            .2byte 0                     /* Boot protocol option flags */
cmdline_size:          .4byte 65536                 /* Maximum size of the kernel command line */
hardware_subarch:      .4byte 0                     /* Hardware subarchitecture */
hardware_subarch_data: .8byte 0                     /* Subarchitecture-specific data */
payload_offset:        .4byte 0                     /* Offset of kernel payload */
payload_length:        .4byte 0                     /* Length of kernel payload */
setup_data:            .8byte 0                     /* 64bit pointer to linked list of struct setup_data */
pref_address:          .8byte vmlinuz_load_address  /* Preferred loading address */
init_size:             .4byte vmlinuz_size          /* Linear memory required during initialization */
handover_offset:       .4byte 0                     /* Offset of handover entry point */

.text
    # The address of the boot_params structure is passed in the RSI
    # register so store it in RDI register so that it can be received
    # by the extract_linux_boot_params fuction later
    mov %esi, %edi
    mov $0x7c00, %esp # Allocate some temporary stack
    jmp *start32_from_vmlinuz
