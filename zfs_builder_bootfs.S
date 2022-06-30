.pushsection .data
.global bootfs_start
.hidden bootfs_start
bootfs_start:
.incbin "zfs_builder_bootfs.bin"
.popsection
