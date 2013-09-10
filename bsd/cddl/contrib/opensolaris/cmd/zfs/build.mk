
zfs-cmd-file-list = zfs_iter zfs_main

zfs-cmd-objects = $(foreach x, $(zfs-cmd-file-list), bsd/cddl/contrib/opensolaris/cmd/zfs/$x.o)

cflags-zfs-cmd-includes = $(cflags-libzfs-include)
# -I$(src)/bsd/cddl/contrib/opensolaris/cmd/stat/common

$(zfs-cmd-objects): kernel-defines =

$(zfs-cmd-objects): local-includes += $(cflags-zfs-cmd-includes)

$(zfs-cmd-objects): CFLAGS += -Wno-switch -D__va_list=__builtin_va_list '-DTEXT_DOMAIN=""' \
			-Wno-maybe-uninitialized -Wno-unused-variable -Wno-unknown-pragmas -Wno-unused-function


zfs.so: $(zfs-cmd-objects) libzfs.so
	$(q-build-so)