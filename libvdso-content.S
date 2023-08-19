.pushsection .data
.align 4096
.global libvdso_start
.hidden libvdso_start
libvdso_start:
.incbin "libvdso-stripped.so"
.popsection
