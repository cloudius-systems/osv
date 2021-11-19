.pushsection .data
.global bootfs_start
.hidden bootfs_start
bootfs_start:
.incbin "bootfs.bin"
.popsection
