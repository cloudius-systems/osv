SECTIONS
{
    . = 0x200000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .eh_frame : { *(.eh_frame) }
    init_array_start = .;
    .init_array : { *(.init_array) }
    init_array_end = .;
    .data : { *(.data) }
    .bss : { *(.bss) }
}
ENTRY(start32);
