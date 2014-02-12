/*
 * Copyright (C) 2014 Eduardo Piva
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
OUTPUT_ARCH(i386)
OUTPUT_FORMAT(elf32-i386)
SECTIONS
{
    . = 0x1801000;
    .text : { *(.text) }
    .data : { *(.data) }
    .bss : { *(.bss) }
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
} ENTRY(uncompress_loader)
