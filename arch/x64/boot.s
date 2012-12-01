
.text
.code32

mb_magic = 0x1BADB002
mb_flags = 0x00000001    # flags: 4k module alignment

multiboot:
    .long mb_magic
    .long mb_flags
    .long 0-mb_magic-mb_flags

.globl start32
start32:
    jmp start32
