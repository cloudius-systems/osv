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
    .rodata : { *(.rodata*) } :text
    .eh_frame : { *(.eh_frame) } :text
    .data : { *(.data) } :text
    _init_array_start = .;
    .init_array : { *(.init_array) } :text
    _init_array_end = .;
    .bss : { *(.bss) } :text
}
PHDRS {
	text PT_LOAD FILEHDR PHDRS;
	dynamic PT_DYNAMIC;
}
ENTRY(start32);
