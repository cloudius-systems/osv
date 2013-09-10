libuutil-file-list = uu_alloc uu_avl uu_dprintf uu_ident uu_list uu_misc uu_open uu_pname uu_string uu_strtoint
libuutil-objects = $(foreach file, $(libuutil-file-list), bsd/cddl/contrib/opensolaris/lib/libuutil/common/$(file).o)

define libuutil-includes =
  bsd/cddl/contrib/opensolaris/lib/libuutil/common
  bsd/cddl/compat/opensolaris/include 
  bsd/sys/cddl/contrib/opensolaris/uts/common
  bsd/sys/cddl/compat/opensolaris
  bsd/cddl/contrib/opensolaris/head
  bsd/include
endef

cflags-libuutil-include = $(foreach path, $(strip $(libuutil-includes)), -isystem $(src)/$(path))

$(libuutil-objects): local-includes += $(cflags-libuutil-include)

# disable the main bsd include search order, we want it before osv but after solaris
$(libuutil-objects): post-includes-bsd =

$(libuutil-objects): kernel-defines =

$(libuutil-objects): CFLAGS += -Wno-unknown-pragmas

libuutil.so: $(libuutil-objects)
	$(makedir)
	$(q-build-so)

