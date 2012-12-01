SECTIONS
{
    . = 0x20000;
    .text : { *(.text) }
    .data : { *(.data) }
    .bss : { *(.bss) }
}
ENTRY(start32);
