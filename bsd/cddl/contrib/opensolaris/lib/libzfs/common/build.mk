# missing: fru sendrecv
libzfs-file-list = changelist config dataset diff import iter mount pool status util
libzfs-objects = $(foreach file, $(libzfs-file-list), bsd/cddl/contrib/opensolaris/lib/libzfs/common/libzfs_$(file).o)

libzpool-file-list = taskq util kernel
libzpool-objects = $(foreach file, $(libzpool-file-list), bsd/cddl/contrib/opensolaris/lib/libzpool/common/$(file).o)

libzfs-objects += $(libzpool-objects)
libzfs-objects += bsd/cddl/compat/opensolaris/misc/mkdirp.o
libzfs-objects += bsd/cddl/compat/opensolaris/misc/zmount.o

define libzfs-includes
  bsd/cddl/compat/opensolaris/lib/libumem
  bsd/cddl/contrib/opensolaris/head
  bsd/cddl/contrib/opensolaris/lib/libzpool/common
  bsd/cddl/contrib/opensolaris/lib/libuutil/common
  bsd/cddl/compat/opensolaris/include
  bsd/cddl/contrib/opensolaris/lib/libzfs/common
  bsd/cddl/contrib/opensolaris/lib/libnvpair
  bsd/lib/libgeom
  bsd/sys/cddl/compat/opensolaris
  bsd/sys/cddl/contrib/opensolaris/uts/common
  bsd/sys/cddl/contrib/opensolaris/uts/common/sys
  bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs
  bsd/sys/cddl/contrib/opensolaris/common/zfs
  bsd/sys/cddl/contrib/opensolaris/uts/common/zmod
  bsd/include
  bsd
  bsd/sys
endef

cflags-libzfs-include = $(foreach path, $(strip $(libzfs-includes)), -isystem $(src)/$(path))

$(libzfs-objects): local-includes += $(cflags-libzfs-include)

# disable the main bsd include search order, we want it before osv but after solaris
$(libzfs-objects): post-includes-bsd =

$(libzfs-objects): kernel-defines =

$(libzfs-objects): CFLAGS += -Wno-switch -D__va_list=__builtin_va_list '-DTEXT_DOMAIN=""' \
			-Wno-maybe-uninitialized -Wno-unused-variable -Wno-unknown-pragmas -Wno-unused-function

libzfs.so: $(libzfs-objects) libuutil.so
	$(makedir)
	$(q-build-so)

