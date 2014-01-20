
zpool-cmd-file-list = zpool_iter  zpool_main  zpool_util  zpool_vdev

zpool-cmd-objects = $(foreach x, $(zpool-cmd-file-list), bsd/cddl/contrib/opensolaris/cmd/zpool/$x.o)

cflags-zpool-cmd-includes = $(cflags-libzfs-include) -I$(src)/bsd/cddl/contrib/opensolaris/cmd/stat/common

$(zpool-cmd-objects): kernel-defines =

$(zpool-cmd-objects): CFLAGS += -D_GNU_SOURCE

$(zpool-cmd-objects): local-includes += $(cflags-zpool-cmd-includes)

$(zpool-cmd-objects): CFLAGS += -Wno-switch -D__va_list=__builtin_va_list '-DTEXT_DOMAIN=""' \
			-Wno-maybe-uninitialized -Wno-unused-variable -Wno-unknown-pragmas -Wno-unused-function


zpool.so: $(zpool-cmd-objects) libzfs.so
	$(q-build-so)