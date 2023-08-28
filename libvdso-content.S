.pushsection .data
#ifdef __x86_64__
.align 4096
#endif
#ifdef __aarch64__
.align 16
#endif
.global libvdso_start
.hidden libvdso_start
libvdso_start:
.incbin "libvdso-stripped.so"
.popsection
