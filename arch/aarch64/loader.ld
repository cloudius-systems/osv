/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
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
    . = 0x40091000;

    .dynamic : ALIGN(16) { *(.dynamic) } : dynamic : text

    .rela.dyn : ALIGN(4096) {
      *(.rela.init)
      *(.rela.text .rela.text.* .rela.gnu.linkonce.t.*)
      *(.rela.fini)
      *(.rela.rodata .rela.rodata.* .rela.gnu.linkonce.r.*)
      *(.rela.data .rela.data.* .rela.gnu.linkonce.d.*)
      *(.rela.tdata .rela.tdata.* .rela.gnu.linkonce.td.*)
      *(.rela.tbss .rela.tbss.* .rela.gnu.linkonce.tb.*)
      *(.rela.ctors)
      *(.rela.dtors)
      *(.rela.got)
      *(.rela.bss .rela.bss.* .rela.gnu.linkonce.b.*)
      *(.rela.ifunc)
     } : dynamic : text

     .rela.plt : ALIGN(4096) {
      *(.rela.plt)
      __rela_iplt_start = .;
      *(.rela.iplt)
      __rela_iplt_end = .;
     } : dynamic : text

    .plt : ALIGN (4096) { *(.plt) *(.iplt) } : dynamic : text
    .got : ALIGN (4096) { *(.got) *(.igot) } : dynamic : text
    .got.plt : ALIGN (4096) { *(.got.plt) *(.igot.plt) } : dynamic : text

    .text : ALIGN (16) {
        text_start = .;
        *(.text.unlikely .text.*_unlikely .text.unlikely.*)
        *(.text.exit .text.exit.*)
        *(.text.fixup)
        *(.text.startup .text.startup.*)
        *(.text.hot .text.hot.*)
        *(.text .stub .text.* .gnu.linkonce.t.*)
        text_end = .;
    } : text

    . = ALIGN(16);
    .fixup : {
        fault_fixup_start = .;
        *(.fixup)
        fault_fixup_end = .;
    } : text

    .eh_frame : { *(.eh_frame) } : text
    .eh_frame_hdr : { *(.eh_frame_hdr) } :text :eh_frame
    .rodata : { *(.rodata*) } :text
    .note : { *(.note*) } :text :note

    .gcc_except_table : { *(.gcc_except_table) *(.gcc_except_table.*) } : text

    .tracepoint_patch_sites : ALIGN(8) {
        __tracepoint_patch_sites_start = .;
        *(.tracepoint_patch_sites)
        __tracepoint_patch_sites_end = .;
    } : text

    .data.rel.ro : { *(.data.rel.ro.local* .gnu.linkonce.d.rel.ro.local.*) *(.data.rel.ro .data.rel.ro.* .gnu.linkonce.d.rel.ro.*) } : text

    .data : { *(.data .data.*) } : text

    _init_array_start = .;
    .init_array : ALIGN(16) {
        *(SORT_BY_NAME(.preinit_array.*) .preinit_array)
        *(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*))
        *(.init_array .ctors)
    } : text
    _init_array_end = .;

    .percpu : ALIGN (4096) {
        _percpu_start = .;
        *(.percpu)
        . = ALIGN(4096);
        _percpu_end = .;
    }
    .percpu_workers : ALIGN (4096) {
        _percpu_workers_start = .;
        *(.percpu_workers)
        _percpu_workers_end = .;
    }
    /* do not align tdata, tbss with .tdata : ALIGN (64),
       or the linker will offset the TLS loads accordingly! */
    .tdata : { *(.tdata .tdata.* .gnu.linkonce.td.*) } : tls : text
    .tbss : { *(.tbss .tbss.* .gnu.linkonce.tb.*) *(.tcommon) } : tls : text
    .tls_template_size = SIZEOF(.tdata) + SIZEOF(.tbss);
    .bss : { *(.dynbss .bss .bss.* .gnu.linkonce.b.*) } : text

    . = ALIGN(64);
    tcb0 = .;
    . = . + .tls_template_size + 1024;
    . = ALIGN(4096);
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
ENTRY(start_elf);
