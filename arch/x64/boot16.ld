OUTPUT_FORMAT(binary)

MEMORY
{
	BOOTSECT : ORIGIN = 0, LENGTH = 0x10000
}

SECTIONS
{
	.text 0x7c00 : { *(.text) } > BOOTSECT
}

