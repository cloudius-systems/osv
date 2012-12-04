SECTIONS
{
    . = 0x200000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .eh_frame : { *(.eh_frame) }
    .data : { *(.data) }
    .bss : { *(.bss) }
}
ENTRY(start32);
