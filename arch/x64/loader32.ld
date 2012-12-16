OUTPUT_FORMAT(elf32-i386)

SECTIONS
{
    .text 0x1ff000 : { *(.text*) }
}
ENTRY(start32);
