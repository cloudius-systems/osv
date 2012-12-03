.pushsection .data
.global bootfs_start
bootfs_start:
.incbin "bootfs.bin"
.popsection
