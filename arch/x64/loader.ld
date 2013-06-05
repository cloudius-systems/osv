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
    .text : { *(.text*) } :text
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
    .tls_template_start = .;
    .tdata : { *(.tdata .tdata.* .gnu.linkonce.td.*) } :tls :text
    .tbss : { *(.tbss .tbss.* .gnu.linkonce.tb.*) } :tls :text
    .tls_template_size = . - .tls_template_start;
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
