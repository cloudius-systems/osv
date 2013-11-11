/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

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
    . = 0x201000;
    .dynamic : { *(.dynamic) } :dynamic :text
    .text : {
        text_start = .;
        *(.text.hot .text.hot.*)
        *(.text.unlikely .text.*_unlikely)
        *(.text.fixup)
        *(.text.startup .text.startup.*)
        *(.text .text.*)
        text_end = .;
    } :text
    . = ALIGN(8);
    .fixup : {
        fault_fixup_start = .;
        *(.fixup)
        fault_fixup_end = .;
    } :text
    .eh_frame : { *(.eh_frame) } : text
    .rodata : { *(.rodata*) } :text
    .eh_frame : { *(.eh_frame) } :text
    .eh_frame_hdr : { *(.eh_frame_hdr) } :text :eh_frame
    .note : { *(.note*) } :text :note
    .tracepoint_patch_sites ALIGN(8) : {
        __tracepoint_patch_sites_start = .;
        *(.tracepoint_patch_sites)
        __tracepoint_patch_sites_end = .;
    } : text
    .data : { *(.data) } :text
    _init_array_start = .;
    .init_array : { *(SORT_BY_INIT_PRIORITY(.init_array.*)) } :text
    _init_array_end = .;
    . = ALIGN(4096);
    .percpu : {
        _percpu_start = .;
        *(.percpu)
        . = ALIGN(4096);
        _percpu_end = .;
        . += ((_percpu_end - _percpu_start) * 64);
        _percpu_sec_end = .;
    }
    .percpu_workers : {
        _percpu_workers_start = .;
        *(.percpu_workers)
        _percpu_workers_end = .;
    }
    . = ALIGN(64);
    .tdata : { *(.tdata .tdata.* .gnu.linkonce.td.*) } :tls :text
    .tbss : {
        *(.tbss .tbss.* .gnu.linkonce.tb.*)
        . = ALIGN(64);
    } :tls :text
    .tls_template_size = SIZEOF(.tdata) + SIZEOF(.tbss);
    .bss : { *(.bss .bss.*) } :text
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
ENTRY(start32);
