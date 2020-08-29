/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

INCLUDE "loader_options.ld"
INCLUDE "libc/aliases.ld"
SECTIONS
{
	/* Set the initial program counter to one page beyond the minimal
	 * aligned segment size.  This allows the ELF header to reside with
	 * the text segment, which is useful since we need the ELF header
	 * to link against libraries later on.
	 *
	 * We can't export the ELF header base as a symbol, because ld
	 * insists on moving stuff around if we do.
	 */
    . = OSV_KERNEL_VM_BASE + 0x800;
	/*
	 * Place address of start32 routine at predefined offset in memory
	 */
    .start32_address : AT(ADDR(.start32_address) - OSV_KERNEL_VM_SHIFT) {
        *(.start32_address)
    }
    . = OSV_KERNEL_VM_BASE + 0x900;
	/*
	 * Place address of start32_from_vmlinuz routine at predefined offset in memory
	 */
    .start32_from_vmlinuz_address : AT(ADDR(.start32_from_vmlinuz_address) - OSV_KERNEL_VM_SHIFT) {
        *(.start32_from_vmlinuz_address)
    }
    . = OSV_KERNEL_VM_BASE + 0x1000;
    .dynamic : AT(ADDR(.dynamic) - OSV_KERNEL_VM_SHIFT) { *(.dynamic) } :dynamic :text
    .text : AT(ADDR(.text) - OSV_KERNEL_VM_SHIFT) {
        text_start = .;
        *(.text.hot .text.hot.*)
        *(.text.unlikely .text.*_unlikely)
        *(.text.fixup)
        *(.text.startup .text.startup.*)
        *(.text .text.*)
        text_end = .;
        PROVIDE(low_vmlinux_entry64 = vmlinux_entry64 - OSV_KERNEL_VM_SHIFT);
    } :text
    . = ALIGN(8);
    .fixup : AT(ADDR(.fixup) - OSV_KERNEL_VM_SHIFT) {
        fault_fixup_start = .;
        *(.fixup)
        fault_fixup_end = .;
    } :text

    . = ALIGN(8);
    .memcpy_decode : AT(ADDR(.memcpy_decode) - OSV_KERNEL_VM_SHIFT) {
        memcpy_decode_start = .;
        *(.memcpy_decode)
        memcpy_decode_end = .;
    } :text

    .eh_frame : AT(ADDR(.eh_frame) - OSV_KERNEL_VM_SHIFT) { *(.eh_frame) } : text
    .rodata : AT(ADDR(.rodata) - OSV_KERNEL_VM_SHIFT) { *(.rodata*) } :text
    .eh_frame : AT(ADDR(.eh_frame) - OSV_KERNEL_VM_SHIFT) { *(.eh_frame) } :text
    .eh_frame_hdr : AT(ADDR(.eh_frame_hdr) - OSV_KERNEL_VM_SHIFT) { *(.eh_frame_hdr) } :text :eh_frame
    .note : AT(ADDR(.note) - OSV_KERNEL_VM_SHIFT) { *(.note*) } :text :note
    .gcc_except_table : AT(ADDR(.gcc_except_table) - OSV_KERNEL_VM_SHIFT) { *(.gcc_except_table) *(.gcc_except_table.*) } : text
    .tracepoint_patch_sites ALIGN(8) : AT(ADDR(.tracepoint_patch_sites) - OSV_KERNEL_VM_SHIFT) {
        __tracepoint_patch_sites_start = .;
        *(.tracepoint_patch_sites)
        __tracepoint_patch_sites_end = .;
    } : text
    .data.rel.ro : AT(ADDR(.data.rel.ro) - OSV_KERNEL_VM_SHIFT) { *(.data.rel.ro.local* .gnu.linkonce.d.rel.ro.local.*) *(.data.rel.ro .data.rel.ro.* .gnu.linkonce.d.rel.ro.*) } : text
    .data : AT(ADDR(.data) - OSV_KERNEL_VM_SHIFT) { *(.data) } :text
    _init_array_start = .;
    .init_array : AT(ADDR(.init_array) - OSV_KERNEL_VM_SHIFT) {
        *(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*))
        *(.init_array .ctors)
    } : text
    _init_array_end = .;
    . = ALIGN(4096);
    .percpu : AT(ADDR(.percpu) - OSV_KERNEL_VM_SHIFT) {
        _percpu_start = .;
        *(.percpu)
        . = ALIGN(4096);
        _percpu_end = .;
    }
    .percpu_workers : AT(ADDR(.percpu_workers) - OSV_KERNEL_VM_SHIFT) {
        _percpu_workers_start = .;
        *(.percpu_workers)
        _percpu_workers_end = .;
    }
    . = ALIGN(64);
    .tdata : AT(ADDR(.tdata) - OSV_KERNEL_VM_SHIFT) { *(.tdata .tdata.* .gnu.linkonce.td.*) } :tls :text
    .tbss : AT(ADDR(.tbss) - OSV_KERNEL_VM_SHIFT) {
        *(.tbss .tbss.* .gnu.linkonce.tb.*)
        _pie_static_tls_start = .;
        /* This is a reserve intended for executables' (pie or non-pie) TLS block */
        . = . + APP_LOCAL_EXEC_TLS_SIZE;
        . = ALIGN(64);
        _pie_static_tls_end = .;
    } :tls :text
    .tls_template_size = SIZEOF(.tdata) + SIZEOF(.tbss);
    .bss : AT(ADDR(.bss) - OSV_KERNEL_VM_SHIFT) { *(.bss .bss.*) } :text
    . = ALIGN(64);
    tcb0 = .;
    . = . + .tls_template_size + 256;
    .edata = .;
    .debug_info 0 : { *(.debug_info .gnu.linkonce.wi.*) }
    .debug_srcinfo 0 : { *(.debug_srcinfo) }
    .debug_sfnames 0 : { *(.debug_sfnames) }
    .debug_pubnames 0 : { *(.debug_pubnames) }
    .debug_frame 0 : { *(.debug_frame) }
    .debug_abbrev 0 : { *(.debug_abbrev) }
    .debug_aranges 0 : { *(.debug_aranges) }
    .debug_ranges 0 : { *(.debug_ranges) }
    .debug_line 0 : { *(.debug_line) }
    .debug_loc 0 : { *(.debug_loc) }
    .debug_str 0 : { *(.debug_str) }
    .debug_macinfo 0 : { *(.debug_macinfo) }
    .debug_typenames 0 : { *(.debug_typenames) }
    .debug_varnames 0 : { *(.debug_varnames) }
    .debug_weaknames 0 : { *(.debug_weaknames) }
    .gdb_index 0 : { *(.gdb_index) }
    .comment : { *(.comment) }
}
PHDRS {
	text PT_LOAD FILEHDR PHDRS;
	tls PT_TLS;
	dynamic PT_DYNAMIC;
	eh_frame PT_GNU_EH_FRAME;
	note PT_NOTE;
}
ENTRY(low_vmlinux_entry64);
