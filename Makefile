# OSv makefile
#
# Copyright (C) 2015 Cloudius Systems, Ltd.
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.

# Delete the builtin make rules, as if "make -r" was used.
.SUFFIXES:

# Ask make to not delete "intermediate" results, such as the .o in the chain
# .cc -> .o -> .so. Otherwise, during the first build, make considers the .o
# to be intermediate, and deletes it, but the newly-created ".d" files lists
# the ".o" as a target - so it needs to be created again on the second make.
# See commit fac05c95 for a longer explanation.
.SECONDARY:

# Deleting partially-build targets on error should be the default, but it
# isn't, for historical reasons, so we need to turn it on explicitly...
.DELETE_ON_ERROR:
###########################################################################
# Backward-compatibility hack to support the old "make ... image=..." image
# building syntax, and pass it into scripts/build. We should eventually drop
# this support and turn the deprecated messages into errors.
compat_args=$(if $(usrskel), usrskel=$(usrskel),)
compat_args+=$(if $(fs), fs=$(fs),)
ifdef image
#$(error Please use scripts/build to build images)
$(info "make image=..." deprecated. Please use "scripts/build image=...".)
default_target:
	./scripts/build image=$(image) $(compat_args)
endif
ifdef modules
#$(error Please use scripts/build to build images)
$(info "make modules=..." deprecated. Please use "scripts/build modules=...".)
default_target:
	./scripts/build modules=$(modules) $(compat_args)
endif
.PHONY: default_target

###########################################################################

include conf/base.mk

# The build mode defaults to "release" (optimized build), the other option
# is "debug" (unoptimized build). In the latter the optimizer interferes
# less with the debugging, but the release build is fully debuggable too.
mode=release
ifeq (,$(wildcard conf/$(mode).mk))
    $(error unsupported mode $(mode))
endif
include conf/$(mode).mk


# By default, detect HOST_CXX's architecture - x64 or aarch64.
# But also allow the user to specify a cross-compiled target architecture
# by setting either "ARCH" or "arch" in the make command line, or the "ARCH"
# environment variable.
HOST_CXX := g++

detect_arch = $(word 1, $(shell { echo "x64        __x86_64__";  \
                                  echo "aarch64    __aarch64__"; \
                       } | $1 -E -xc - | grep ' 1$$'))

host_arch := $(call detect_arch, $(HOST_CXX))

# As an alternative to setting ARCH or arch, let's allow the user to
# directly set the CROSS_PREFIX environment variable, and learn its arch:
ifdef CROSS_PREFIX
    ARCH := $(call detect_arch, $(CROSS_PREFIX)gcc)
endif

ifndef ARCH
    ARCH := $(host_arch)
endif
arch := $(ARCH)

# ARCH_STR is like ARCH, but uses the full name x86_64 instead of x64
ARCH_STR := $(arch:x64=x86_64)

ifeq (,$(wildcard conf/$(arch).mk))
    $(error unsupported architecture $(arch))
endif
include conf/$(arch).mk

# This parameter can be passed in to the build command to specify name of
# a drivers profile. The drivers profile allows to build custom kernel with
# a specific set of drivers enabled in the corresponding makefile include
# file - conf/profiles/$(arch)/$(drivers_profile).mk). The default profile is
# 'all' which incorporates all drivers into kernel.
# In general the profiles set variables named conf_drivers_*, which then in turn
# are used in the rules below to decide which object files are linked into
# kernel.
drivers_profile?=all
ifeq (,$(wildcard conf/profiles/$(arch)/$(drivers_profile).mk))
    $(error unsupported drivers profile $(drivers_profile))
endif
include conf/profiles/$(arch)/$(drivers_profile).mk
# The base profile disables all drivers unless they are explicitly enabled
# by the profile file included in the line above. The base profile also enforces
# certain dependencies between drivers, for example the ide driver needs pci support, etc.
# For more details please read comments in the profile file.
include conf/profiles/$(arch)/base.mk

CROSS_PREFIX ?= $(if $(filter-out $(arch),$(host_arch)),$(arch)-linux-gnu-)
CXX=$(CROSS_PREFIX)g++
CC=$(CROSS_PREFIX)gcc
LD=$(CROSS_PREFIX)ld.bfd
export STRIP=$(CROSS_PREFIX)strip
OBJCOPY=$(CROSS_PREFIX)objcopy

# Our makefile puts all compilation results in a single directory, $(out),
# instead of mixing them with the source code. This allows us to compile
# different variants of the code - for different mode (release or debug)
# or arch (x86 or aarch64) side by side. It also makes "make clean" very
# simple, as all compilation results are in $(out) and can be removed in
# one fell swoop.
out = build/$(mode).$(arch)
outlink = build/$(mode)
outlink2 = build/last

ifneq ($(MAKECMDGOALS),clean)
$(info Building into $(out))
endif

###########################################################################

# We need some external git modules to have been downloaded, because the
# default "make" depends on the following directories:
#   musl/ -  for some of the header files (symbolic links in include/api) and
#            some of the source files ($(musl) below).
#   external/x64/acpica - for the ACPICA library (see $(acpi) below).
# Additional submodules are need when certain make parameters are used.
ifeq (,$(wildcard musl/include))
    $(error Missing musl/ directory. Please run "git submodule update --init --recursive")
endif
ifeq (,$(wildcard external/x64/acpica/source))
    $(error Missing external/x64/acpica/ directory. Please run "git submodule update --init --recursive")
endif

# This makefile wraps all commands with the $(quiet) or $(very-quiet) macros
# so that instead of half-a-screen-long command lines we short summaries
# like "CC file.cc". These macros also keep the option of viewing the
# full command lines, if you wish, with "make V=1".
quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)

all: $(out)/loader.img links $(out)/kernel.elf
ifeq ($(arch),x64)
all: $(out)/vmlinuz.bin
endif
.PHONY: all

links:
	$(call very-quiet, ln -nsf $(notdir $(out)) $(outlink))
	$(call very-quiet, ln -nsf $(notdir $(out)) $(outlink2))
.PHONY: links

check:
	./scripts/build check
.PHONY: check

# Remember that "make clean" needs the same parameters that set $(out) in
# the first place, so to clean the output of "make mode=debug" you need to
# do "make mode=debug clean".
clean:
	rm -rf $(out)
	rm -f $(outlink) $(outlink2)
.PHONY: clean

# Manually listing recompilation dependencies in the Makefile (such as which
# object needs to be recompiled when a header changed) is antediluvian.
# Even "makedepend" is old school! The best modern technique for automatic
# dependency generation, which we use here, works like this:
# We note that before the first compilation, we don't need to know these
# dependencies at all, as everything will be compiled anyway. But during
# this compilation, we pass to the compiler a special option (-MD) which
# causes it to also output a file with suffix ".d" listing the dependencies
# discovered during the compilation of that source file. From then on,
# on every compilation we "include" all the ".d" files generated in the
# previous compilation, and create new ".d" when a source file changed
# (and therefore got recompiled).
ifneq ($(MAKECMDGOALS),clean)
include $(shell test -d $(out) && find $(out) -name '*.d')
endif

# Before we can try to build anything in $(out), we need to make sure the
# directory exists. Unfortunately, this is not quite enough, as when we
# compile somedir/somefile.c to $(out)/somedir/somefile.o, we also need
# to make sure $(out)/somedir exists. This is why we have $(makedir) below.
# I wonder if there's a better way of doing this with dependencies, so make
# will only call mkdir for each directory once.
$(out)/%: | $(out)
$(out):
	mkdir -p $(out)

# "tags" is the default output file of ctags, "TAGS" is that of etags
tags TAGS:
	rm -f -- "$@"
	find . -name "*.cc" -o -name "*.hh" -o -name "*.h" -o -name "*.c" |\
		xargs $(if $(filter $@, tags),ctags,etags) -a
.PHONY: tags TAGS

cscope:
	find -name '*.[chS]' -o -name "*.cc" -o -name "*.hh" | cscope -bq -i-
	@echo cscope index created
.PHONY: cscope

###########################################################################

local-includes =
INCLUDES = $(local-includes) -Iarch/$(arch) -I. -Iinclude  -Iarch/common
INCLUDES += -isystem include/glibc-compat
#
# Let us detect presence of standard C++ headers
CXX_INCLUDES = $(shell $(CXX) -E -xc++ - -v </dev/null 2>&1 | awk '/^End/ {exit} /^ .*c\+\+/ {print "-isystem" $$0}')
ifeq ($(CXX_INCLUDES),)
  ifeq ($(CROSS_PREFIX),aarch64-linux-gnu-)
    # We are on distribution where the aarch64-linux-gnu package does not come with C++ headers
    # So let use point it to the expected location
    aarch64_gccbase = build/downloaded_packages/aarch64/gcc/install
    ifeq (,$(wildcard $(aarch64_gccbase)))
     $(error Missing $(aarch64_gccbase) directory. Please run "./scripts/download_aarch64_packages.py")
    endif

    gcc-inc-base := $(dir $(shell find $(aarch64_gccbase)/ -name vector | grep -v -e debug/vector$$ -e profile/vector$$ -e experimental/vector$$))
    ifeq (,$(gcc-inc-base))
      $(error Could not find C++ headers under $(aarch64_gccbase) directory. Please run "./scripts/download_aarch64_packages.py")
    endif

    gcc-inc-base3 := $(dir $(shell dirname `find $(aarch64_gccbase)/ -name c++config.h | grep -v /32/`))
    ifeq (,$(gcc-inc-base3))
      $(error Could not find C++ headers under $(aarch64_gccbase) directory. Please run "./scripts/download_aarch64_packages.py")
    endif
    CXX_INCLUDES = -isystem $(gcc-inc-base) -isystem $(gcc-inc-base3)

    gcc-inc-base2 := $(dir $(shell find $(aarch64_gccbase)/ -name unwind.h))
    ifeq (,$(gcc-inc-base2))
      $(error Could not find standard gcc headers like "unwind.h" under $(aarch64_gccbase) directory. Please run "./scripts/download_aarch64_packages.py")
    endif
    STANDARD_GCC_INCLUDES = -isystem $(gcc-inc-base2)

    gcc-sysroot = --sysroot $(aarch64_gccbase)
    standard-includes-flag = -nostdinc
  else
    $(error Could not find standard C++ headers. Please run "sudo ./scripts/setup.py")
  endif
else
  # If gcc can find C++ headers it also means it can find standard libc headers, so no need to add them specifically
  STANDARD_GCC_INCLUDES =
  standard-includes-flag =
endif

ifeq ($(arch),x64)
INCLUDES += -isystem external/$(arch)/acpica/source/include
endif

ifeq ($(arch),aarch64)
libfdt_base = external/$(arch)/libfdt
INCLUDES += -isystem $(libfdt_base)
endif

INCLUDES += $(boost-includes)
# Starting in Gcc 6, the standard C++ header files (which we do not change)
# must precede in the include path the C header files (which we replace).
# This is explained in https://gcc.gnu.org/bugzilla/show_bug.cgi?id=70722.
# So we are forced to list here (before include/api) the system's default
# C++ include directories, though they are already in the default search path.
INCLUDES += $(CXX_INCLUDES)
INCLUDES += $(pre-include-api)
INCLUDES += -isystem include/api
INCLUDES += -isystem include/api/$(arch)
# must be after include/api, since it includes some libc-style headers:
INCLUDES += $(STANDARD_GCC_INCLUDES)
INCLUDES += -isystem $(out)/gen/include
INCLUDES += $(post-includes-bsd)

post-includes-bsd += -isystem bsd/sys
# For acessing machine/ in cpp xen drivers
post-includes-bsd += -isystem bsd/
post-includes-bsd += -isystem bsd/$(arch)

$(out)/musl/%.o: pre-include-api = -isystem include/api/internal_musl_headers -isystem musl/src/include

ifneq ($(werror),0)
	CFLAGS_WERROR = -Werror
endif
# $(call compiler-flag, -ffoo, option, file)
#     returns option if file builds with -ffoo, empty otherwise
compiler-flag = $(shell $(CXX) $(CFLAGS_WERROR) $1 -o /dev/null -c $3  > /dev/null 2>&1 && echo $2)

compiler-specific := $(call compiler-flag, -std=gnu++11, -DHAVE_ATTR_COLD_LABEL, compiler/attr/cold-label.cc)

source-dialects = -D_GNU_SOURCE

$(out)/bsd/%.o: source-dialects =

# libc has its own source dialect control
$(out)/libc/%.o: source-dialects =
$(out)/musl/%.o: source-dialects =

# do not hide symbols in musl/libc because it has it's own hiding mechanism
$(out)/libc/%.o: cc-hide-flags =
$(out)/libc/%.o: cxx-hide-flags =
$(out)/musl/%.o: cc-hide-flags =

kernel-defines = -D_KERNEL $(source-dialects) $(cc-hide-flags) $(gc-flags)

# This play the same role as "_KERNEL", but _KERNEL unfortunately is too
# overloaded. A lot of files will expect it to be set no matter what, specially
# in headers. "userspace" inclusion of such headers is valid, and lacking
# _KERNEL will make them fail to compile. That is specially true for the BSD
# imported stuff like ZFS commands.
#
# To add something to the kernel build, you can write for your object:
#
#   mydir/*.o COMMON += <MY_STUFF>
#
# To add something that will *not* be part of the main kernel, you can do:
#
#   mydir/*.o EXTRA_FLAGS = <MY_STUFF>
ifeq ($(arch),x64)
EXTRA_FLAGS = -D__OSV_CORE__ -DOSV_KERNEL_BASE=$(kernel_base) -DOSV_KERNEL_VM_BASE=$(kernel_vm_base) \
	-DOSV_KERNEL_VM_SHIFT=$(kernel_vm_shift) -DOSV_LZKERNEL_BASE=$(lzkernel_base)
else
EXTRA_FLAGS = -D__OSV_CORE__ -DOSV_KERNEL_VM_BASE=$(kernel_vm_base)
endif
EXTRA_LIBS =
COMMON = $(autodepend) -g -Wall -Wno-pointer-arith $(CFLAGS_WERROR) -Wformat=0 -Wno-format-security \
	-D __BSD_VISIBLE=1 -U _FORTIFY_SOURCE -fno-stack-protector $(INCLUDES) \
	$(kernel-defines) \
	-fno-omit-frame-pointer $(compiler-specific) \
	-include compiler/include/intrinsics.hh \
	$(arch-cflags) $(conf-opt) $(acpi-defines) $(tracing-flags) $(gcc-sysroot) \
	$(configuration) -D__OSV__ -D__XEN_INTERFACE_VERSION__="0x00030207" -DARCH_STRING=$(ARCH_STR) $(EXTRA_FLAGS)
COMMON += $(standard-includes-flag)

tracing-flags-0 =
tracing-flags-1 = -finstrument-functions -finstrument-functions-exclude-file-list=c++,trace.cc,trace.hh,align.hh,mmintrin.h
tracing-flags = $(tracing-flags-$(conf-tracing))

cc-hide-flags-0 =
cc-hide-flags-1 = -fvisibility=hidden
cc-hide-flags = $(cc-hide-flags-$(conf_hide_symbols))

cxx-hide-flags-0 =
cxx-hide-flags-1 = -fvisibility-inlines-hidden
cxx-hide-flags = $(cxx-hide-flags-$(conf_hide_symbols))

gc-flags-0 =
gc-flags-1 = -ffunction-sections -fdata-sections
gc-flags = $(gc-flags-$(conf_hide_symbols))

gcc-opt-Og := $(call compiler-flag, -Og, -Og, compiler/empty.cc)

CXXFLAGS = -std=gnu++11 $(COMMON) $(cxx-hide-flags)
CFLAGS = -std=gnu99 $(COMMON)

# should be limited to files under libc/ eventually
CFLAGS += -I libc/stdio -I libc/internal -I libc/arch/$(arch) \
	-Wno-missing-braces -Wno-parentheses -Wno-unused-but-set-variable

ASFLAGS = -g $(autodepend) -D__ASSEMBLY__

$(out)/fs/vfs/main.o: CXXFLAGS += -Wno-sign-compare -Wno-write-strings

$(out)/bsd/%.o: INCLUDES += -isystem bsd/sys
$(out)/bsd/%.o: INCLUDES += -isystem bsd/
# for machine/
$(out)/bsd/%.o: INCLUDES += -isystem bsd/$(arch)

configuration-defines = conf-preempt conf-debug_memory conf-logger_debug conf-debug_elf

configuration = $(foreach cf,$(configuration-defines), \
                      -D$(cf:conf-%=CONF_%)=$($(cf)))



makedir = $(call very-quiet, mkdir -p $(dir $@))
build-so = $(CC) $(CFLAGS) -o $@ $^ $(EXTRA_LIBS)
q-build-so = $(call quiet, $(build-so), LINK $@)


$(out)/%.o: %.cc | generated-headers
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) -c -o $@ $<, CXX $*.cc)

$(out)/%.o: %.c | generated-headers
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -c -o $@ $<, CC $*.c)

$(out)/%.o: %.S
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) $(ASFLAGS) -c -o $@ $<, AS $*.S)

$(out)/%.o: %.s
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) $(ASFLAGS) -c -o $@ $<, AS $*.s)

%.so: EXTRA_FLAGS = -fPIC -shared -z relro -z lazy
%.so: %.o
	$(makedir)
	$(q-build-so)

autodepend = -MD -MT $@ -MP

tools := tools/mkfs/mkfs.so tools/cpiod/cpiod.so

$(out)/tools/%.o: COMMON += -fPIC

tools += tools/uush/uush.so
tools += tools/uush/ls.so
tools += tools/uush/mkdir.so

tools += tools/mount/mount-fs.so
tools += tools/mount/umount.so

ifeq ($(arch),aarch64)
# note that the bootfs.manifest entry for the uush image
# has no effect on the loader image, only on the usr image.
# The only thing that does have an effect is the
# bootfs.manifest.skel.
#
# Therefore, you need to manually add tests/tst-hello.so
# to the bootfs.manifest.skel atm to get it to work.
#
tools += tests/tst-hello.so
cmdline = --nomount tests/tst-hello.so
endif

$(out)/loader-stripped.elf: $(out)/loader.elf
	$(call quiet, $(STRIP) $(out)/loader.elf -o $(out)/loader-stripped.elf, STRIP loader.elf -> loader-stripped.elf )

ifeq ($(arch),x64)

# kernel_base is where the kernel will be loaded after uncompression.
# lzkernel_base is where the compressed kernel is loaded from disk.
kernel_base := 0x200000
lzkernel_base := 0x100000
kernel_vm_base := 0x40200000

# the default of 64 bytes can be overridden by passing the app_local_exec_tls_size
# environment variable to the make or scripts/build
app_local_exec_tls_size := 0x40

$(out)/arch/x64/boot16.o: $(out)/lzloader.elf
$(out)/boot.bin: arch/x64/boot16.ld $(out)/arch/x64/boot16.o
	$(call quiet, $(LD) -o $@ -T $^, LD $@)

image-size = $(shell stat --printf %s $(out)/lzloader.elf)

$(out)/loader.img: $(out)/boot.bin $(out)/lzloader.elf
	$(call quiet, dd if=$(out)/boot.bin of=$@ > /dev/null 2>&1, DD loader.img boot.bin)
	$(call quiet, dd if=$(out)/lzloader.elf of=$@ conv=notrunc seek=128 > /dev/null 2>&1, \
		DD loader.img lzloader.elf)
	$(call quiet, scripts/imgedit.py setsize "-f raw $@" $(image-size), IMGEDIT $@)
	$(call quiet, scripts/imgedit.py setargs "-f raw $@" $(cmdline), IMGEDIT $@)

kernel_size = $(shell stat --printf %s $(out)/loader-stripped.elf)

$(out)/arch/x64/vmlinuz-boot32.o: $(out)/loader-stripped.elf
$(out)/arch/x64/vmlinuz-boot32.o: ASFLAGS += -I$(out) -DOSV_KERNEL_SIZE=$(kernel_size)

$(out)/vmlinuz-boot.bin: $(out)/arch/x64/vmlinuz-boot32.o arch/x64/vmlinuz-boot.ld
	$(call quiet, $(LD) -static -o $@ \
		$(filter-out %.bin, $(^:%.ld=-T %.ld)), LD $@)

$(out)/vmlinuz.bin: $(out)/vmlinuz-boot.bin $(out)/loader-stripped.elf
	$(call quiet, dd if=$(out)/vmlinuz-boot.bin of=$@ > /dev/null 2>&1, DD vmlinuz.bin vmlinuz-boot.bin)
	$(call quiet, dd if=$(out)/loader-stripped.elf of=$@ conv=notrunc seek=4 > /dev/null 2>&1, \
		DD vmlinuz.bin loader-stripped.elf)

$(out)/fastlz/fastlz.o:
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) -O2 -m32 -fno-instrument-functions -o $@ -c fastlz/fastlz.cc, CXX fastlz/fastlz.cc)

$(out)/fastlz/lz: fastlz/fastlz.cc fastlz/lz.cc | generated-headers
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) -O2 -o $@ $(filter %.cc, $^), CXX $@)

$(out)/loader-stripped.elf.lz.o: $(out)/loader-stripped.elf $(out)/fastlz/lz
	$(call quiet, $(out)/fastlz/lz $(out)/loader-stripped.elf, LZ loader-stripped.elf)
	$(call quiet, cd $(out); objcopy -B i386 -I binary -O elf32-i386 loader-stripped.elf.lz loader-stripped.elf.lz.o, OBJCOPY loader-stripped.elf.lz -> loader-stripped.elf.lz.o)

$(out)/fastlz/lzloader.o: fastlz/lzloader.cc | generated-headers
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) -O0 -m32 -fno-instrument-functions -o $@ -c fastlz/lzloader.cc, CXX $<)

$(out)/lzloader.elf: $(out)/loader-stripped.elf.lz.o $(out)/fastlz/lzloader.o arch/x64/lzloader.ld \
	$(out)/fastlz/fastlz.o
	$(call very-quiet, scripts/check-image-size.sh $(out)/loader-stripped.elf)
	$(call quiet, $(LD) -o $@ --defsym=OSV_LZKERNEL_BASE=$(lzkernel_base) \
		-Bdynamic --export-dynamic --eh-frame-hdr --enable-new-dtags -z max-page-size=4096 \
		-T arch/x64/lzloader.ld \
		$(filter %.o, $^), LINK lzloader.elf)
	$(call quiet, truncate -s %32768 $@, ALIGN lzloader.elf)

acpi-defines = -DACPI_MACHINE_WIDTH=64 -DACPI_USE_LOCAL_CACHE

acpi-source := $(shell find external/$(arch)/acpica/source/components -type f -name '*.c')
acpi = $(patsubst %.c, %.o, $(acpi-source))

$(acpi:%=$(out)/%): CFLAGS += -fno-strict-aliasing -Wno-stringop-truncation

kernel_vm_shift := $(shell printf "0x%X" $(shell expr $$(( $(kernel_vm_base) - $(kernel_base) )) ))

endif # x64

ifeq ($(arch),aarch64)

kernel_vm_base := 0xfc0080000 #63GB
app_local_exec_tls_size := 0x40

include $(libfdt_base)/Makefile.libfdt
libfdt-source := $(patsubst %.c, $(libfdt_base)/%.c, $(LIBFDT_SRCS))
libfdt = $(patsubst %.c, %.o, $(libfdt-source))

$(out)/preboot.elf: arch/$(arch)/preboot.ld $(out)/arch/$(arch)/preboot.o
	$(call quiet, $(LD) -o $@ -T $^, LD $@)

$(out)/preboot.bin: $(out)/preboot.elf
	$(call quiet, $(OBJCOPY) -O binary $^ $@, OBJCOPY $@)

edata = $(shell readelf --syms $(out)/loader.elf | grep "\.edata" | awk '{print "0x" $$2}')
image_size = $$(( $(edata) - $(kernel_vm_base) ))

$(out)/loader.img: $(out)/preboot.bin $(out)/loader-stripped.elf
	$(call quiet, dd if=$(out)/preboot.bin of=$@ > /dev/null 2>&1, DD $@ preboot.bin)
	$(call quiet, dd if=$(out)/loader-stripped.elf of=$@ conv=notrunc obs=4096 seek=16 > /dev/null 2>&1, DD $@ loader-stripped.elf)
	$(call quiet, scripts/imgedit.py setsize_aarch64 "-f raw $@" $(image_size), IMGEDIT $@)
	$(call quiet, scripts/imgedit.py setargs "-f raw $@" $(cmdline), IMGEDIT $@)

endif # aarch64

$(out)/bsd/sys/crypto/rijndael/rijndael-api-fst.o: COMMON+=-fno-strict-aliasing
$(out)/bsd/sys/crypto/sha2/sha2.o: COMMON+=-fno-strict-aliasing
$(out)/bsd/sys/net/route.o: COMMON+=-fno-strict-aliasing
$(out)/bsd/sys/net/rtsock.o: COMMON+=-fno-strict-aliasing
$(out)/bsd/sys/net/in.o: COMMON+=-fno-strict-aliasing
$(out)/bsd/sys/net/if.o: COMMON+=-fno-strict-aliasing
$(out)/bsd/sys/netinet/in_rmx.o: COMMON+=-fno-strict-aliasing
$(out)/bsd/sys/netinet/ip_input.o: COMMON+=-fno-strict-aliasing
$(out)/bsd/sys/netinet/in.o: COMMON+=-fno-strict-aliasing

$(out)/bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/metaslab.o: COMMON+=-Wno-tautological-compare

# A lot of the BSD code used to be C code, which commonly bzero()ed or
# memcpy()ed objects. In C++, this should not be done (objects have
# constructors and assignments), and gcc 8 starts to warn about it.
# Instead of fixing all these occurances, let's ask gcc to ignore this
# warning. At least for now.
$(out)/bsd/%.o: CXXFLAGS += -Wno-class-memaccess

bsd  = bsd/init.o
bsd += bsd/net.o
bsd += bsd/$(arch)/machine/in_cksum.o
bsd += bsd/sys/crypto/rijndael/rijndael-alg-fst.o
bsd += bsd/sys/crypto/rijndael/rijndael-api.o
bsd += bsd/sys/crypto/rijndael/rijndael-api-fst.o
bsd += bsd/sys/crypto/sha2/sha2.o
bsd += bsd/sys/libkern/arc4random.o
bsd += bsd/sys/libkern/random.o
bsd += bsd/sys/libkern/inet_ntoa.o
bsd += bsd/sys/libkern/inet_aton.o
bsd += bsd/sys/kern/md5c.o
bsd += bsd/sys/kern/kern_mbuf.o
bsd += bsd/sys/kern/uipc_mbuf.o
bsd += bsd/sys/kern/uipc_mbuf2.o
bsd += bsd/sys/kern/uipc_domain.o
bsd += bsd/sys/kern/uipc_sockbuf.o
bsd += bsd/sys/kern/uipc_socket.o
bsd += bsd/sys/kern/uipc_syscalls.o
bsd += bsd/sys/kern/uipc_syscalls_wrap.o
bsd += bsd/sys/kern/subr_sbuf.o
bsd += bsd/sys/kern/subr_eventhandler.o
bsd += bsd/sys/kern/subr_hash.o
bsd += bsd/sys/kern/subr_taskqueue.o
$(out)/bsd/sys/kern/subr_taskqueue.o: COMMON += -Wno-dangling-pointer
bsd += bsd/sys/kern/sys_socket.o
bsd += bsd/sys/kern/subr_disk.o
bsd += bsd/porting/route.o
bsd += bsd/porting/networking.o
bsd += bsd/porting/netport.o
bsd += bsd/porting/netport1.o
bsd += bsd/porting/shrinker.o
bsd += bsd/porting/cpu.o
bsd += bsd/porting/uma_stub.o
bsd += bsd/porting/sync_stub.o
bsd += bsd/porting/callout.o
bsd += bsd/porting/synch.o
bsd += bsd/porting/kthread.o
bsd += bsd/porting/mmu.o
bsd += bsd/porting/pcpu.o
bsd += bsd/porting/bus_dma.o
bsd += bsd/sys/netinet/if_ether.o
bsd += bsd/sys/compat/linux/linux_socket.o
bsd += bsd/sys/compat/linux/linux_ioctl.o
bsd += bsd/sys/compat/linux/linux_netlink.o
bsd += bsd/sys/net/if_ethersubr.o
bsd += bsd/sys/net/if_llatbl.o
bsd += bsd/sys/net/radix.o
bsd += bsd/sys/net/route.o
bsd += bsd/sys/net/raw_cb.o
bsd += bsd/sys/net/raw_usrreq.o
bsd += bsd/sys/net/rtsock.o
bsd += bsd/sys/net/netisr.o
bsd += bsd/sys/net/netisr1.o
bsd += bsd/sys/net/if_dead.o
bsd += bsd/sys/net/if_clone.o
bsd += bsd/sys/net/if_loop.o
bsd += bsd/sys/net/if.o
bsd += bsd/sys/net/pfil.o
bsd += bsd/sys/net/routecache.o
bsd += bsd/sys/netinet/in.o
bsd += bsd/sys/netinet/in_pcb.o
bsd += bsd/sys/netinet/in_proto.o
bsd += bsd/sys/netinet/in_mcast.o
$(out)/bsd/sys/netinet/in_mcast.o: COMMON += -Wno-maybe-uninitialized
bsd += bsd/sys/netinet/in_rmx.o
bsd += bsd/sys/netinet/ip_id.o
bsd += bsd/sys/netinet/ip_icmp.o
bsd += bsd/sys/netinet/ip_input.o
bsd += bsd/sys/netinet/ip_output.o
bsd += bsd/sys/netinet/ip_options.o
bsd += bsd/sys/netinet/raw_ip.o
bsd += bsd/sys/netinet/igmp.o
bsd += bsd/sys/netinet/udp_usrreq.o
bsd += bsd/sys/netinet/tcp_debug.o
bsd += bsd/sys/netinet/tcp_hostcache.o
bsd += bsd/sys/netinet/tcp_input.o
bsd += bsd/sys/netinet/tcp_lro.o
bsd += bsd/sys/netinet/tcp_output.o
bsd += bsd/sys/netinet/tcp_reass.o
bsd += bsd/sys/netinet/tcp_sack.o
bsd += bsd/sys/netinet/tcp_subr.o
bsd += bsd/sys/netinet/tcp_syncache.o
bsd += bsd/sys/netinet/tcp_timer.o
bsd += bsd/sys/netinet/tcp_timewait.o
bsd += bsd/sys/netinet/tcp_usrreq.o
bsd += bsd/sys/netinet/cc/cc.o
bsd += bsd/sys/netinet/cc/cc_cubic.o
bsd += bsd/sys/netinet/cc/cc_htcp.o
bsd += bsd/sys/netinet/cc/cc_newreno.o
bsd += bsd/sys/netinet/arpcache.o
ifeq ($(conf_drivers_xen),1)
bsd += bsd/sys/xen/evtchn.o
endif

ifeq ($(arch),x64)
$(out)/bsd/%.o: COMMON += -DXEN -DXENHVM
ifeq ($(conf_drivers_xen),1)
bsd += bsd/sys/xen/gnttab.o
bsd += bsd/sys/xen/xenstore/xenstore.o
bsd += bsd/sys/xen/xenbus/xenbus.o
bsd += bsd/sys/xen/xenbus/xenbusb.o
bsd += bsd/sys/xen/xenbus/xenbusb_front.o
bsd += bsd/sys/dev/xen/netfront/netfront.o
bsd += bsd/sys/dev/xen/blkfront/blkfront.o
endif
ifeq ($(conf_drivers_hyperv),1)
bsd += bsd/sys/dev/hyperv/vmbus/hyperv.o
endif
endif

bsd += bsd/sys/dev/random/hash.o
bsd += bsd/sys/dev/random/randomdev_soft.o
bsd += bsd/sys/dev/random/yarrow.o
bsd += bsd/sys/dev/random/random_harvestq.o
bsd += bsd/sys/dev/random/harvest.o
bsd += bsd/sys/dev/random/live_entropy_sources.o

$(out)/bsd/sys/%.o: COMMON += -Wno-sign-compare -Wno-narrowing -Wno-write-strings -Wno-parentheses -Wno-unused-but-set-variable

xdr :=
xdr += bsd/sys/xdr/xdr.o
xdr += bsd/sys/xdr/xdr_array.o
xdr += bsd/sys/xdr/xdr_mem.o

solaris :=
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_atomic.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_cmn_err.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_kmem.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_kobj.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_kstat.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_policy.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_sunddi.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_string.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_sysevent.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_taskq.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_uio.o
solaris += bsd/sys/cddl/contrib/opensolaris/common/acl/acl_common.o
solaris += bsd/sys/cddl/contrib/opensolaris/common/avl/avl.o
solaris += bsd/sys/cddl/contrib/opensolaris/common/nvpair/fnvpair.o
solaris += bsd/sys/cddl/contrib/opensolaris/common/nvpair/nvpair.o
solaris += bsd/sys/cddl/contrib/opensolaris/common/nvpair/nvpair_alloc_fixed.o
solaris += bsd/sys/cddl/contrib/opensolaris/common/unicode/u8_textprep.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/os/callb.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/os/fm.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/os/list.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/os/nvpair_alloc_system.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/adler32.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/deflate.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/inffast.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/inflate.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/inftrees.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/opensolaris_crc32.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/trees.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/zmod.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/zmod_subr.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/zutil.o

zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zfeature_common.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zfs_comutil.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zfs_deleg.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zfs_fletcher.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zfs_ioctl_compat.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zfs_namecheck.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zfs_prop.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zpool_prop.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zprop_common.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/arc.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/bplist.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/bpobj.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/bptree.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dbuf.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/ddt.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/ddt_zap.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu.o
#zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu_diff.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu_object.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu_objset.o
#zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu_send.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu_traverse.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu_tx.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu_zfetch.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dnode.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dnode_sync.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_dataset.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_deadlist.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_deleg.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_dir.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_pool.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_prop.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_scan.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_synctask.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/gzip.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/lzjb.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/metaslab.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/refcount.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/rrwlock.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/sa.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/sha256.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/spa.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/space_map.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/spa_config.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/spa_errlog.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/spa_history.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/spa_misc.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/txg.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/uberblock.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/unique.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_cache.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_disk.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_file.o
#zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_geom.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_label.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_mirror.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_missing.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_queue.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_raidz.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_root.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zap.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zap_leaf.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zap_micro.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfeature.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_acl.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_byteswap.o
#zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_ctldir.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_debug.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_dir.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_fm.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_fuid.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_ioctl.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_init.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_log.o
#zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_onexit.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_replay.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_rlock.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_sa.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_vfsops.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_vnops.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_znode.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zil.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zio.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zio_checksum.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zio_compress.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zio_inject.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zle.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zrlock.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zvol.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/lz4.o

solaris += $(zfs)

$(zfs:%=$(out)/%): CFLAGS+= \
	-DBUILDING_ZFS \
	-Wno-array-bounds \
	-Ibsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs \
	-Ibsd/sys/cddl/contrib/opensolaris/common/zfs

$(solaris:%=$(out)/%): CFLAGS+= \
	-fno-strict-aliasing \
	-Wno-unknown-pragmas \
	-Wno-unused-variable \
	-Wno-switch \
	-Wno-maybe-uninitialized \
	-Ibsd/sys/cddl/compat/opensolaris \
	-Ibsd/sys/cddl/contrib/opensolaris/common \
	-Ibsd/sys/cddl/contrib/opensolaris/uts/common \
	-Ibsd/sys

$(solaris:%=$(out)/%): ASFLAGS+= \
	-Ibsd/sys/cddl/contrib/opensolaris/uts/common


libtsm :=
libtsm += drivers/libtsm/tsm_render.o
libtsm += drivers/libtsm/tsm_screen.o
libtsm += drivers/libtsm/tsm_vte.o
libtsm += drivers/libtsm/tsm_vte_charsets.o

drivers := $(bsd)
drivers += core/mmu.o
drivers += arch/$(arch)/early-console.o
drivers += drivers/console.o
drivers += drivers/console-multiplexer.o
drivers += drivers/console-driver.o
drivers += drivers/line-discipline.o
drivers += drivers/clock.o
drivers += drivers/clock-common.o
drivers += drivers/clockevent.o
drivers += drivers/isa-serial-base.o
drivers += core/elf.o
$(out)/core/elf.o: CXXFLAGS += -DHIDE_SYMBOLS=$(conf_hide_symbols)
drivers += drivers/random.o
drivers += drivers/zfs.o
drivers += drivers/null.o
drivers += drivers/device.o
ifeq ($(conf_drivers_pci),1)
drivers += drivers/pci-generic.o
drivers += drivers/pci-device.o
drivers += drivers/pci-function.o
drivers += drivers/pci-bridge.o
endif
drivers += drivers/driver.o

ifeq ($(arch),x64)
ifeq ($(conf_drivers_vga),1)
drivers += $(libtsm)
drivers += drivers/vga.o
endif
drivers += drivers/kbd.o drivers/isa-serial.o
drivers += arch/$(arch)/pvclock-abi.o

ifeq ($(conf_drivers_virtio),1)
drivers += drivers/virtio.o
ifeq ($(conf_drivers_pci),1)
drivers += drivers/virtio-pci-device.o
endif
drivers += drivers/virtio-vring.o
ifeq ($(conf_drivers_mmio),1)
drivers += drivers/virtio-mmio.o
endif
drivers += drivers/virtio-net.o
drivers += drivers/virtio-blk.o
drivers += drivers/virtio-scsi.o
drivers += drivers/virtio-rng.o
drivers += drivers/virtio-fs.o
endif

ifeq ($(conf_drivers_vmxnet3),1)
drivers += drivers/vmxnet3.o
drivers += drivers/vmxnet3-queues.o
endif
drivers += drivers/kvmclock.o
ifeq ($(conf_drivers_hyperv),1)
drivers += drivers/hypervclock.o
endif
ifeq ($(conf_drivers_acpi),1)
drivers += drivers/acpi.o
endif
ifeq ($(conf_drivers_hpet),1)
drivers += drivers/hpet.o
endif
ifeq ($(conf_drivers_pvpanic),1)
drivers += drivers/pvpanic.o
endif
drivers += drivers/rtc.o
ifeq ($(conf_drivers_ahci),1)
drivers += drivers/ahci.o
endif
ifeq ($(conf_drivers_scsi),1)
drivers += drivers/scsi-common.o
endif
ifeq ($(conf_drivers_ide),1)
drivers += drivers/ide.o
endif
ifeq ($(conf_drivers_pvscsi),1)
drivers += drivers/vmw-pvscsi.o
endif

ifeq ($(conf_drivers_xen),1)
drivers += drivers/xenclock.o
drivers += drivers/xenfront.o drivers/xenfront-xenbus.o drivers/xenfront-blk.o
drivers += drivers/xenplatform-pci.o
endif
endif # x64

ifeq ($(arch),aarch64)
drivers += drivers/mmio-isa-serial.o
drivers += drivers/pl011.o
drivers += drivers/pl031.o
ifeq ($(conf_drivers_cadence),1)
drivers += drivers/cadence-uart.o
endif
ifeq ($(conf_drivers_xen),1)
drivers += drivers/xenconsole.o
endif

ifeq ($(conf_drivers_virtio),1)
drivers += drivers/virtio.o
ifeq ($(conf_drivers_pci),1)
drivers += drivers/virtio-pci-device.o
endif
ifeq ($(conf_drivers_mmio),1)
drivers += drivers/virtio-mmio.o
endif
drivers += drivers/virtio-vring.o
drivers += drivers/virtio-rng.o
drivers += drivers/virtio-blk.o
drivers += drivers/virtio-net.o
drivers += drivers/virtio-fs.o
endif
endif # aarch64

objects += arch/$(arch)/arch-trace.o
objects += arch/$(arch)/arch-setup.o
objects += arch/$(arch)/signal.o
objects += arch/$(arch)/arch-cpu.o
objects += arch/$(arch)/backtrace.o
objects += arch/$(arch)/smp.o
objects += arch/$(arch)/elf-dl.o
objects += arch/$(arch)/entry.o
objects += arch/$(arch)/mmu.o
objects += arch/$(arch)/exceptions.o
objects += arch/$(arch)/dump.o
objects += arch/$(arch)/arch-elf.o
objects += arch/$(arch)/cpuid.o
objects += arch/$(arch)/firmware.o
objects += arch/$(arch)/hypervisor.o
objects += arch/$(arch)/interrupt.o
ifeq ($(conf_drivers_pci),1)
objects += arch/$(arch)/pci.o
objects += arch/$(arch)/msi.o
endif
objects += arch/$(arch)/power.o
objects += arch/$(arch)/feexcept.o
ifeq ($(conf_drivers_xen),1)
objects += arch/$(arch)/xen.o
endif

$(out)/arch/x64/string-ssse3.o: CXXFLAGS += -mssse3

ifeq ($(arch),aarch64)
objects += arch/$(arch)/psci.o
objects += arch/$(arch)/arm-clock.o
objects += arch/$(arch)/gic.o
objects += arch/$(arch)/arch-dtb.o
objects += arch/$(arch)/hypercall.o
objects += arch/$(arch)/memset.o
objects += arch/$(arch)/memcpy.o
objects += arch/$(arch)/memmove.o
objects += arch/$(arch)/tlsdesc.o
objects += arch/$(arch)/sched.o
objects += $(libfdt)
endif

ifeq ($(arch),x64)
objects += arch/x64/dmi.o
objects += arch/x64/string.o
objects += arch/x64/string-ssse3.o
objects += arch/x64/arch-trace.o
objects += arch/x64/ioapic.o
objects += arch/x64/apic.o
objects += arch/x64/apic-clock.o
objects += arch/x64/entry-xen.o
objects += arch/x64/vmlinux.o
objects += arch/x64/vmlinux-boot64.o
objects += arch/x64/pvh-boot.o
ifeq ($(conf_drivers_acpi),1)
objects += $(acpi)
endif
endif # x64

ifeq ($(conf_drivers_xen),1)
objects += core/xen_intr.o
endif
objects += core/math.o
objects += core/spinlock.o
objects += core/lfmutex.o
objects += core/rwlock.o
objects += core/semaphore.o
objects += core/condvar.o
objects += core/debug.o
objects += core/rcu.o
objects += core/pagecache.o
objects += core/mempool.o
objects += core/alloctracker.o
objects += core/printf.o
objects += core/sampler.o

objects += linux.o
objects += core/commands.o
objects += core/sched.o
objects += core/mmio.o
objects += core/kprintf.o
objects += core/trace.o
objects += core/trace-count.o
objects += core/callstack.o
objects += core/poll.o
objects += core/select.o
objects += core/epoll.o
objects += core/newpoll.o
objects += core/power.o
objects += core/percpu.o
objects += core/per-cpu-counter.o
objects += core/percpu-worker.o
objects += core/dhcp.o
objects += core/run.o
objects += core/shutdown.o
objects += core/version.o
objects += core/waitqueue.o
objects += core/chart.o
objects += core/net_channel.o
objects += core/demangle.o
objects += core/async.o
objects += core/net_trace.o
objects += core/app.o
objects += core/libaio.o
objects += core/osv_execve.o
objects += core/osv_c_wrappers.o
objects += core/options.o

#include $(src)/libc/build.mk:
libc =
libc_to_hide =
musl =
environ_libc =
environ_musl =

ifeq ($(arch),x64)
musl_arch = x86_64
else
musl_arch = aarch64
endif

libc += internal/_chk_fail.o
libc_to_hide += internal/_chk_fail.o
libc += internal/floatscan.o
libc += internal/intscan.o
libc += internal/libc.o
libc += internal/shgetc.o

musl += ctype/__ctype_get_mb_cur_max.o
musl += ctype/__ctype_tolower_loc.o
musl += ctype/__ctype_toupper_loc.o
musl += ctype/isalnum.o
musl += ctype/isalpha.o
musl += ctype/isascii.o
musl += ctype/isblank.o
musl += ctype/iscntrl.o
musl += ctype/isdigit.o
musl += ctype/isgraph.o
musl += ctype/islower.o
musl += ctype/isprint.o
musl += ctype/ispunct.o
musl += ctype/isspace.o
musl += ctype/isupper.o
musl += ctype/iswalnum.o
musl += ctype/iswalpha.o
musl += ctype/iswblank.o
musl += ctype/iswcntrl.o
musl += ctype/iswctype.o
musl += ctype/iswdigit.o
musl += ctype/iswgraph.o
musl += ctype/iswlower.o
musl += ctype/iswprint.o
musl += ctype/iswpunct.o
musl += ctype/iswspace.o
musl += ctype/iswupper.o
musl += ctype/iswxdigit.o
musl += ctype/isxdigit.o
musl += ctype/toascii.o
musl += ctype/tolower.o
musl += ctype/toupper.o
musl += ctype/towctrans.o
musl += ctype/wcswidth.o
musl += ctype/wctrans.o
musl += ctype/wcwidth.o

musl += dirent/alphasort.o
musl += dirent/scandir.o

libc += env/__environ.o
musl += env/clearenv.o
musl += env/getenv.o
libc += env/secure_getenv.o
musl += env/putenv.o
musl += env/setenv.o
musl += env/unsetenv.o

environ_libc += env/__environ.c
environ_musl += env/clearenv.c
environ_musl += env/getenv.c
environ_libc += env/secure_getenv.c
environ_musl += env/putenv.c
environ_musl += env/setenv.c
environ_musl += env/unsetenv.c
environ_musl += string/strchrnul.c

musl += ctype/__ctype_b_loc.o

musl += errno/strerror.o
libc += errno/strerror.o

musl += locale/catclose.o
musl += locale/__mo_lookup.o
$(out)/musl/src/locale/__mo_lookup.o: CFLAGS += $(cc-hide-flags-$(conf_hide_symbols))
musl += locale/pleval.o
musl += locale/catgets.o
libc += locale/catopen.o
libc += locale/duplocale.o
libc += locale/freelocale.o
musl += locale/iconv.o
musl += locale/iconv_close.o
libc += locale/intl.o
libc += locale/langinfo.o
musl += locale/localeconv.o
libc += locale/setlocale.o
musl += locale/strcoll.o
musl += locale/strfmon.o
libc += locale/strtod_l.o
libc += locale/strtof_l.o
libc += locale/strtold_l.o
musl += locale/strxfrm.o
libc += locale/uselocale.o
musl += locale/wcscoll.o
musl += locale/wcsxfrm.o

musl += math/__cos.o
musl += math/__cosdf.o
musl += math/__cosl.o
musl += math/__expo2.o
musl += math/__expo2f.o
musl += math/__fpclassify.o
musl += math/__fpclassifyf.o
musl += math/__fpclassifyl.o
musl += math/__invtrigl.o
musl += math/__polevll.o
musl += math/__rem_pio2.o
musl += math/__rem_pio2_large.o
$(out)/musl/src/math/__rem_pio2_large.o: CFLAGS += -Wno-maybe-uninitialized
musl += math/__rem_pio2f.o
musl += math/__rem_pio2l.o
musl += math/__signbit.o
musl += math/__signbitf.o
musl += math/__signbitl.o
musl += math/__sin.o
musl += math/__sindf.o
musl += math/__sinl.o
musl += math/__tan.o
musl += math/__tandf.o
musl += math/__tanl.o
musl += math/__math_oflow.o
musl += math/__math_oflowf.o
musl += math/__math_xflow.o
musl += math/__math_xflowf.o
musl += math/__math_uflow.o
musl += math/__math_uflowf.o
musl += math/__math_divzero.o
musl += math/__math_divzerof.o
musl += math/__math_invalid.o
musl += math/__math_invalidf.o
musl += math/acos.o
musl += math/acosf.o
musl += math/acosh.o
musl += math/acoshf.o
musl += math/acoshl.o
musl += math/acosl.o
musl += math/asin.o
musl += math/asinf.o
musl += math/asinh.o
musl += math/asinhf.o
musl += math/asinhl.o
musl += math/asinl.o
musl += math/atan.o
musl += math/atan2.o
musl += math/atan2f.o
musl += math/atan2l.o
musl += math/atanf.o
musl += math/atanh.o
musl += math/atanhf.o
musl += math/atanhl.o
musl += math/atanl.o
musl += math/cbrt.o
musl += math/cbrtf.o
musl += math/cbrtl.o
musl += math/ceil.o
musl += math/ceilf.o
musl += math/ceill.o
musl += math/copysign.o
musl += math/copysignf.o
musl += math/copysignl.o
musl += math/cos.o
musl += math/cosf.o
musl += math/cosh.o
musl += math/coshf.o
musl += math/coshl.o
musl += math/cosl.o
musl += math/erf.o
musl += math/erff.o
musl += math/erfl.o
musl += math/exp.o
musl += math/exp_data.o
musl += math/exp10.o
musl += math/exp10f.o
musl += math/exp10l.o
musl += math/exp2.o
musl += math/exp2f.o
musl += math/exp2f_data.o
musl += math/exp2l.o
$(out)/musl/src/math/exp2l.o: CFLAGS += -Wno-unused-variable
musl += math/expf.o
musl += math/expl.o
musl += math/expm1.o
musl += math/expm1f.o
musl += math/expm1l.o
musl += math/fabs.o
musl += math/fabsf.o
musl += math/fabsl.o
musl += math/fdim.o
musl += math/fdimf.o
musl += math/fdiml.o
musl += math/floor.o
musl += math/floorf.o
musl += math/floorl.o
#musl += math/fma.o
#musl += math/fmaf.o
#musl += math/fmal.o
musl += math/fmax.o
musl += math/fmaxf.o
musl += math/fmaxl.o
musl += math/fmin.o
musl += math/fminf.o
musl += math/fminl.o
musl += math/fmod.o
musl += math/fmodf.o
musl += math/fmodl.o
musl += math/finite.o
musl += math/finitef.o
libc += math/finitel.o
musl += math/frexp.o
musl += math/frexpf.o
musl += math/frexpl.o
musl += math/hypot.o
musl += math/hypotf.o
musl += math/hypotl.o
musl += math/ilogb.o
$(out)/musl/src/math/ilogb.o: CFLAGS += -Wno-unknown-pragmas
musl += math/ilogbf.o
$(out)/musl/src/math/ilogbf.o: CFLAGS += -Wno-unknown-pragmas
musl += math/ilogbl.o
$(out)/musl/src/math/ilogbl.o: CFLAGS += -Wno-unknown-pragmas
musl += math/j0.o
musl += math/j0f.o
musl += math/j1.o
musl += math/j1f.o
musl += math/jn.o
musl += math/jnf.o
musl += math/ldexp.o
musl += math/ldexpf.o
musl += math/ldexpl.o
musl += math/lgamma.o
musl += math/lgamma_r.o
$(out)/musl/src/math/lgamma_r.o: CFLAGS += -Wno-maybe-uninitialized
musl += math/lgammaf.o
musl += math/lgammaf_r.o
$(out)/musl/src/math/lgammaf_r.o: CFLAGS += -Wno-maybe-uninitialized
musl += math/lgammal.o
$(out)/musl/src/math/lgammal.o: CFLAGS += -Wno-maybe-uninitialized
#musl += math/llrint.o
#musl += math/llrintf.o
#musl += math/llrintl.o
musl += math/llround.o
musl += math/llroundf.o
musl += math/llroundl.o
musl += math/log.o
musl += math/log_data.o
musl += math/log10.o
musl += math/log10f.o
musl += math/log10l.o
musl += math/log1p.o
musl += math/log1pf.o
musl += math/log1pl.o
musl += math/log2.o
musl += math/log2_data.o
musl += math/log2f.o
musl += math/log2f_data.o
musl += math/log2l.o
musl += math/logb.o
musl += math/logbf.o
musl += math/logbl.o
musl += math/logf.o
musl += math/logf_data.o
musl += math/logl.o
musl += math/lrint.o
#musl += math/lrintf.o
#musl += math/lrintl.o
musl += math/lround.o
musl += math/lroundf.o
musl += math/lroundl.o
musl += math/modf.o
musl += math/modff.o
musl += math/modfl.o
musl += math/nan.o
musl += math/nanf.o
musl += math/nanl.o
musl += math/nearbyint.o
$(out)/musl/src/math/nearbyint.o: CFLAGS += -Wno-unknown-pragmas
musl += math/nearbyintf.o
$(out)/musl/src/math/nearbyintf.o: CFLAGS += -Wno-unknown-pragmas
musl += math/nearbyintl.o
$(out)/musl/src/math/nearbyintl.o: CFLAGS += -Wno-unknown-pragmas
musl += math/nextafter.o
musl += math/nextafterf.o
musl += math/nextafterl.o
musl += math/nexttoward.o
musl += math/nexttowardf.o
musl += math/nexttowardl.o
musl += math/pow.o
musl += math/pow_data.o
musl += math/powf.o
musl += math/powf_data.o
musl += math/powl.o
musl += math/remainder.o
musl += math/remainderf.o
musl += math/remainderl.o
musl += math/remquo.o
musl += math/remquof.o
musl += math/remquol.o
musl += math/rint.o
musl += math/rintf.o
musl += math/rintl.o
musl += math/round.o
musl += math/roundf.o
musl += math/roundl.o
musl += math/scalb.o
musl += math/scalbf.o
musl += math/scalbln.o
musl += math/scalblnf.o
musl += math/scalblnl.o
musl += math/scalbn.o
musl += math/scalbnf.o
musl += math/scalbnl.o
musl += math/signgam.o
musl += math/significand.o
musl += math/significandf.o
musl += math/sin.o
musl += math/sincos.o
musl += math/sincosf.o
msul += math/sincosl.o
musl += math/sinf.o
musl += math/sinh.o
musl += math/sinhf.o
musl += math/sinhl.o
musl += math/sinl.o
musl += math/sqrt.o
musl += math/sqrtf.o
musl += math/sqrtl.o
musl += math/tan.o
musl += math/tanf.o
musl += math/tanh.o
musl += math/tanhf.o
musl += math/tanhl.o
musl += math/tanl.o
musl += math/tgamma.o
musl += math/tgammaf.o
musl += math/tgammal.o
musl += math/trunc.o
musl += math/truncf.o
musl += math/truncl.o

# Issue #867: Gcc 4.8.4 has a bug where it optimizes the trivial round-
# related functions incorrectly - it appears to convert calls to any
# function called round() to calls to a function called lround() -
# and similarly for roundf() and roundl().
# None of the specific "-fno-*" options disable this buggy optimization,
# unfortunately. The simplest workaround is to just disable optimization
# for the affected files.
$(out)/musl/src/math/lround.o: conf-opt := $(conf-opt) -O0
$(out)/musl/src/math/lroundf.o: conf-opt := $(conf-opt) -O0
$(out)/musl/src/math/lroundl.o: conf-opt := $(conf-opt) -O0
$(out)/musl/src/math/llround.o: conf-opt := $(conf-opt) -O0
$(out)/musl/src/math/llroundf.o: conf-opt := $(conf-opt) -O0
$(out)/musl/src/math/llroundl.o: conf-opt := $(conf-opt) -O0

musl += misc/a64l.o
musl += misc/basename.o
musl += misc/dirname.o
libc += misc/error.o
musl += misc/ffs.o
musl += misc/ffsl.o
musl += misc/ffsll.o
musl += misc/get_current_dir_name.o
libc += misc/gethostid.o
libc += misc/getopt.o
libc_to_hide += misc/getopt.o
libc += misc/getopt_long.o
libc_to_hide += misc/getopt_long.o
musl += misc/getsubopt.o
libc += misc/realpath.o
libc += misc/backtrace.o
libc += misc/uname.o
libc += misc/lockf.o
libc += misc/mntent.o
libc_to_hide += misc/mntent.o
musl += misc/nftw.o
libc += misc/__longjmp_chk.o

musl += signal/killpg.o
musl += signal/siginterrupt.o
musl += signal/sigrtmin.o
musl += signal/sigrtmax.o

musl += multibyte/btowc.o
musl += multibyte/internal.o
musl += multibyte/mblen.o
musl += multibyte/mbrlen.o
musl += multibyte/mbrtowc.o
musl += multibyte/mbsinit.o
musl += multibyte/mbsnrtowcs.o
libc += multibyte/__mbsnrtowcs_chk.o
musl += multibyte/mbsrtowcs.o
libc += multibyte/__mbsrtowcs_chk.o
musl += multibyte/mbstowcs.o
libc += multibyte/__mbstowcs_chk.o
musl += multibyte/mbtowc.o
musl += multibyte/wcrtomb.o
musl += multibyte/wcsnrtombs.o
musl += multibyte/wcsrtombs.o
musl += multibyte/wcstombs.o
musl += multibyte/wctob.o
musl += multibyte/wctomb.o

$(out)/libc/multibyte/mbsrtowcs.o: CFLAGS += -Imusl/src/multibyte

musl += network/htonl.o
musl += network/htons.o
musl += network/ntohl.o
musl += network/ntohs.o
libc += network/gethostbyname_r.o
musl += network/gethostbyname2_r.o
musl += network/gethostbyaddr_r.o
musl += network/gethostbyaddr.o
musl += network/resolvconf.o
musl += network/res_msend.o
$(out)/musl/src/network/res_msend.o: CFLAGS += -Wno-maybe-uninitialized --include libc/syscall_to_function.h --include libc/internal/pthread_stubs.h $(cc-hide-flags-$(conf_hide_symbols))
$(out)/libc/multibyte/mbsrtowcs.o: CFLAGS += -Imusl/src/multibyte
musl += network/lookup_ipliteral.o
libc += network/getaddrinfo.o
libc += network/freeaddrinfo.o
musl += network/dn_expand.o
musl += network/res_mkquery.o
musl += network/dns_parse.o
musl += network/in6addr_any.o
musl += network/in6addr_loopback.o
musl += network/lookup_name.o
musl += network/lookup_serv.o
libc += network/getnameinfo.o
libc += network/__dns.o
libc_to_hide += network/__dns.o
libc += network/__ipparse.o
libc_to_hide += network/__ipparse.o
musl += network/inet_addr.o
musl += network/inet_aton.o
musl += network/inet_pton.o
musl += network/inet_ntop.o
musl += network/proto.o
musl += network/if_indextoname.o
$(out)/musl/src/network/if_indextoname.o: CFLAGS += --include libc/syscall_to_function.h --include libc/network/__socket.h -Wno-stringop-truncation
musl += network/if_nametoindex.o
$(out)/musl/src/network/if_nametoindex.o: CFLAGS += --include libc/syscall_to_function.h --include libc/network/__socket.h -Wno-stringop-truncation
musl += network/gai_strerror.o
musl += network/h_errno.o
musl += network/getservbyname_r.o
musl += network/getservbyname.o
musl += network/getservbyport_r.o
musl += network/getservbyport.o
musl += network/getifaddrs.o
musl += network/if_nameindex.o
musl += network/if_freenameindex.o
musl += network/res_init.o
musl += network/netlink.o
$(out)/musl/src/network/netlink.o: CFLAGS += --include libc/syscall_to_function.h --include libc/network/__netlink.h

musl += prng/rand.o
musl += prng/rand_r.o
libc += prng/random.o
musl += prng/__rand48_step.o
musl += prng/__seed48.o
musl += prng/drand48.o
musl += prng/lcong48.o
musl += prng/lrand48.o
musl += prng/mrand48.o
musl += prng/seed48.o
$(out)/musl/src/prng/seed48.o: CFLAGS += -Wno-array-parameter
musl += prng/srand48.o
libc += random.o

libc += process/execve.o
musl += process/execle.o
musl += process/execv.o
musl += process/execl.o
libc += process/waitpid.o
musl += process/wait.o

musl += setjmp/$(musl_arch)/setjmp.o
musl += setjmp/$(musl_arch)/longjmp.o
libc += arch/$(arch)/setjmp/sigsetjmp.o
libc += signal/block.o
libc += signal/siglongjmp.o
ifeq ($(arch),x64)
libc += arch/$(arch)/ucontext/getcontext.o
libc += arch/$(arch)/ucontext/setcontext.o
libc += arch/$(arch)/ucontext/start_context.o
libc_to_hide += arch/$(arch)/ucontext/start_context.o
libc += arch/$(arch)/ucontext/ucontext.o
libc += string/memmove.o
endif

musl += search/tfind.o
musl += search/tsearch.o

musl += stdio/__fclose_ca.o
libc += stdio/__fdopen.o
$(out)/libc/stdio/__fdopen.o: CFLAGS += --include libc/syscall_to_function.h
musl += stdio/__fmodeflags.o
libc += stdio/__fopen_rb_ca.o
libc += stdio/__fprintf_chk.o
libc += stdio/__lockfile.o
musl += stdio/__overflow.o
musl += stdio/__stdio_close.o
$(out)/musl/src/stdio/__stdio_close.o: CFLAGS += --include libc/syscall_to_function.h
musl += stdio/__stdio_exit.o
libc += stdio/__stdio_read.o
musl += stdio/__stdio_seek.o
$(out)/musl/src/stdio/__stdio_seek.o: CFLAGS += --include libc/syscall_to_function.h
musl += stdio/__stdio_write.o
$(out)/musl/src/stdio/__stdio_write.o: CFLAGS += --include libc/syscall_to_function.h
libc += stdio/__stdout_write.o
musl += stdio/__string_read.o
musl += stdio/__toread.o
musl += stdio/__towrite.o
musl += stdio/__uflow.o
libc += stdio/__vfprintf_chk.o
libc += stdio/ofl.o
musl += stdio/ofl_add.o
musl += stdio/asprintf.o
musl += stdio/clearerr.o
musl += stdio/dprintf.o
musl += stdio/ext.o
musl += stdio/ext2.o
musl += stdio/fclose.o
musl += stdio/feof.o
musl += stdio/ferror.o
musl += stdio/fflush.o
libc += stdio/fgetc.o
musl += stdio/fgetln.o
musl += stdio/fgetpos.o
musl += stdio/fgets.o
musl += stdio/fgetwc.o
musl += stdio/fgetws.o
musl += stdio/fileno.o
libc += stdio/flockfile.o
libc += stdio/fmemopen.o
musl += stdio/fopen.o
$(out)/musl/src/stdio/fopen.o: CFLAGS += --include libc/syscall_to_function.h
musl += stdio/fprintf.o
libc += stdio/fputc.o
musl += stdio/fputs.o
musl += stdio/fputwc.o
musl += stdio/fputws.o
musl += stdio/fread.o
libc += stdio/__fread_chk.o
musl += stdio/freopen.o
$(out)/musl/src/stdio/freopen.o: CFLAGS += --include libc/syscall_to_function.h
musl += stdio/fscanf.o
musl += stdio/fseek.o
musl += stdio/fsetpos.o
musl += stdio/ftell.o
libc += stdio/ftrylockfile.o
libc += stdio/funlockfile.o
musl += stdio/fwide.o
musl += stdio/fwprintf.o
musl += stdio/fwrite.o
musl += stdio/fwscanf.o
libc += stdio/getc.o
musl += stdio/getc_unlocked.o
libc += stdio/getchar.o
musl += stdio/getchar_unlocked.o
musl += stdio/getdelim.o
musl += stdio/getline.o
musl += stdio/gets.o
musl += stdio/getw.o
musl += stdio/getwc.o
musl += stdio/getwchar.o
libc += stdio/open_memstream.o
libc += stdio/open_wmemstream.o
musl += stdio/perror.o
musl += stdio/printf.o
libc += stdio/putc.o
musl += stdio/putc_unlocked.o
libc += stdio/putchar.o
musl += stdio/putchar_unlocked.o
musl += stdio/puts.o
musl += stdio/putw.o
musl += stdio/putwc.o
musl += stdio/putwchar.o
libc += stdio/remove.o
musl += stdio/rewind.o
musl += stdio/scanf.o
musl += stdio/setbuf.o
musl += stdio/setbuffer.o
musl += stdio/setlinebuf.o
libc += stdio/setvbuf.o
musl += stdio/snprintf.o
musl += stdio/sprintf.o
libc += stdio/sscanf.o
libc += stdio/stderr.o
libc += stdio/stdin.o
libc += stdio/stdout.o
musl += stdio/swprintf.o
musl += stdio/swscanf.o
musl += stdio/tempnam.o
$(out)/musl/src/stdio/tempnam.o: CFLAGS += --include libc/syscall_to_function.h
musl += stdio/tmpfile.o
$(out)/musl/src/stdio/tmpfile.o: CFLAGS += --include libc/syscall_to_function.h
musl += stdio/tmpnam.o
$(out)/musl/src/stdio/tmpnam.o: CFLAGS += --include libc/syscall_to_function.h
musl += stdio/ungetc.o
musl += stdio/ungetwc.o
musl += stdio/vasprintf.o
libc += stdio/vdprintf.o
libc += stdio/vfprintf.o
$(out)/libc/stdio/vfprintf.o: COMMON += -Wno-maybe-uninitialized
musl += stdio/vfscanf.o
$(out)/musl/src/stdio/vfscanf.o: COMMON += -Wno-maybe-uninitialized
musl += stdio/vfwprintf.o
musl += stdio/vfwscanf.o
$(out)/musl/src/stdio/vfwscanf.o: COMMON += -Wno-maybe-uninitialized
musl += stdio/vprintf.o
musl += stdio/vscanf.o
libc += stdio/vsnprintf.o
musl += stdio/vsprintf.o
libc += stdio/vsscanf.o
libc += stdio/vswprintf.o
libc += stdio/vswscanf.o
musl += stdio/vwprintf.o
musl += stdio/vwscanf.o
musl += stdio/wprintf.o
musl += stdio/wscanf.o
libc += stdio/printf-hooks.o

musl += stdlib/abs.o
musl += stdlib/atof.o
musl += stdlib/atoi.o
musl += stdlib/atol.o
musl += stdlib/atoll.o
musl += stdlib/bsearch.o
musl += stdlib/div.o
musl += stdlib/ecvt.o
musl += stdlib/fcvt.o
musl += stdlib/gcvt.o
musl += stdlib/imaxabs.o
musl += stdlib/imaxdiv.o
musl += stdlib/labs.o
musl += stdlib/ldiv.o
musl += stdlib/llabs.o
musl += stdlib/lldiv.o
musl += stdlib/qsort.o
$(out)/musl/src/stdlib/qsort.o: COMMON += -Wno-dangling-pointer
libc += stdlib/qsort_r.o
$(out)/libc/stdlib/qsort_r.o: COMMON += -Wno-dangling-pointer
libc += stdlib/strtol.o
libc += stdlib/strtod.o
libc += stdlib/wcstol.o

libc += string/__memcpy_chk.o
libc += string/explicit_bzero.o
libc += string/__explicit_bzero_chk.o
musl += string/bcmp.o
musl += string/bcopy.o
musl += string/bzero.o
musl += string/index.o
musl += string/memccpy.o
musl += string/memchr.o
musl += string/memcmp.o
libc += string/memcpy.o
libc_to_hide += string/memcpy.o
musl += string/memmem.o
musl += string/mempcpy.o
musl += string/memrchr.o
libc += string/__memmove_chk.o
libc += string/memset.o
libc_to_hide += string/memset.o
libc += string/__memset_chk.o
libc += string/rawmemchr.o
musl += string/rindex.o
musl += string/stpcpy.o
libc += string/__stpcpy_chk.o
musl += string/stpncpy.o
musl += string/strcasecmp.o
musl += string/strcasestr.o
musl += string/strcat.o
libc += string/__strcat_chk.o
musl += string/strchr.o
musl += string/strchrnul.o
musl += string/strcmp.o
musl += string/strcpy.o
libc += string/__strcpy_chk.o
musl += string/strcspn.o
musl += string/strdup.o
libc += string/strerror_r.o
musl += string/strlcat.o
musl += string/strlcpy.o
musl += string/strlen.o
musl += string/strncasecmp.o
musl += string/strncat.o
libc += string/__strncat_chk.o
musl += string/strncmp.o
musl += string/strncpy.o
libc += string/__strncpy_chk.o
musl += string/strndup.o
musl += string/strnlen.o
musl += string/strpbrk.o
musl += string/strrchr.o
musl += string/strsep.o
libc += string/stresep.o
libc_to_hide += string/stresep.o
musl += string/strsignal.o
musl += string/strspn.o
musl += string/strstr.o
musl += string/strtok.o
musl += string/strtok_r.o
musl += string/strverscmp.o
musl += string/swab.o
musl += string/wcpcpy.o
musl += string/wcpncpy.o
musl += string/wcscasecmp.o
musl += string/wcscasecmp_l.o
musl += string/wcscat.o
musl += string/wcschr.o
musl += string/wcscmp.o
musl += string/wcscpy.o
libc += string/__wcscpy_chk.o
musl += string/wcscspn.o
musl += string/wcsdup.o
musl += string/wcslen.o
musl += string/wcsncasecmp.o
musl += string/wcsncasecmp_l.o
musl += string/wcsncat.o
musl += string/wcsncmp.o
musl += string/wcsncpy.o
musl += string/wcsnlen.o
musl += string/wcspbrk.o
musl += string/wcsrchr.o
musl += string/wcsspn.o
musl += string/wcsstr.o
musl += string/wcstok.o
musl += string/wcswcs.o
musl += string/wmemchr.o
musl += string/wmemcmp.o
musl += string/wmemcpy.o
libc += string/__wmemcpy_chk.o
musl += string/wmemmove.o
libc += string/__wmemmove_chk.o
musl += string/wmemset.o
libc += string/__wmemset_chk.o

musl += temp/__randname.o
musl += temp/mkdtemp.o
musl += temp/mkstemp.o
musl += temp/mktemp.o
musl += temp/mkostemp.o
musl += temp/mkostemps.o

musl += time/__map_file.o
$(out)/musl/src/time/__map_file.o: CFLAGS += --include libc/syscall_to_function.h
musl += time/__month_to_secs.o
musl += time/__secs_to_tm.o
musl += time/__tm_to_secs.o
libc += time/__tz.o
$(out)/libc/time/__tz.o: pre-include-api = -isystem include/api/internal_musl_headers -isystem musl/src/include
musl += time/__year_to_secs.o
musl += time/asctime.o
musl += time/asctime_r.o
musl += time/ctime.o
musl += time/ctime_r.o
musl += time/difftime.o
musl += time/getdate.o
musl += time/gmtime.o
musl += time/gmtime_r.o
musl += time/localtime.o
musl += time/localtime_r.o
musl += time/mktime.o
musl += time/strftime.o
musl += time/strptime.o
musl += time/time.o
musl += time/timegm.o
musl += time/wcsftime.o
musl += time/ftime.o
$(out)/libc/time/ftime.o: CFLAGS += -Ilibc/include

musl += termios/tcflow.o

musl += unistd/sleep.o
musl += unistd/gethostname.o
libc += unistd/sethostname.o
libc += unistd/sync.o
libc += unistd/getpgid.o
libc += unistd/setpgid.o
libc += unistd/getpgrp.o
libc += unistd/getppid.o
libc += unistd/getsid.o
libc += unistd/ttyname_r.o
musl += unistd/ttyname.o
musl += unistd/tcgetpgrp.o
musl += unistd/tcsetpgrp.o
musl += unistd/setpgrp.o

musl += regex/fnmatch.o
musl += regex/glob.o
musl += regex/regcomp.o
$(out)/musl/src/regex/regcomp.o: CFLAGS += -UNDEBUG
musl += regex/regexec.o
$(out)/musl/src/regex/regexec.o: CFLAGS += -UNDEBUG
musl += regex/regerror.o
musl += regex/tre-mem.o
$(out)/musl/src/regex/tre-mem.o: CFLAGS += -UNDEBUG

libc += pthread.o
libc_to_hide += pthread.o
libc += pthread_barrier.o
libc += libc.o
libc += dlfcn.o
libc += time.o
libc_to_hide += time.o
libc += signal.o
libc_to_hide += signal.o
libc += mman.o
libc_to_hide += mman.o
libc += sem.o
libc_to_hide += sem.o
libc += pipe_buffer.o
libc_to_hide += pipe_buffer.o
libc += pipe.o
libc_to_hide += pipe.o
libc += af_local.o
libc_to_hide += af_local.o
libc += user.o
libc += resource.o
libc += mount.o
libc += eventfd.o
libc_to_hide += eventfd.o
libc += timerfd.o
libc_to_hide += timerfd.o
libc += shm.o
libc += inotify.o
libc += __pread64_chk.o
libc += __read_chk.o
libc += syslog.o
libc += cxa_thread_atexit.o
libc += cpu_set.o
libc += malloc_hooks.o
libc += mallopt.o

libc += linux/makedev.o

musl += fenv/fegetexceptflag.o
musl += fenv/feholdexcept.o
musl += fenv/fesetexceptflag.o
musl += fenv/fesetround.o
musl += fenv/$(musl_arch)/fenv.o

musl += crypt/crypt_blowfish.o
musl += crypt/crypt.o
musl += crypt/crypt_des.o
musl += crypt/crypt_md5.o
musl += crypt/crypt_r.o
musl += crypt/crypt_sha256.o
musl += crypt/crypt_sha512.o
musl += crypt/encrypt.o

#include $(src)/fs/build.mk:

fs_objs :=

fs_objs += fs.o \
	unsupported.o

fs_objs += vfs/main.o \
	vfs/kern_descrip.o \
	vfs/kern_physio.o \
	vfs/subr_uio.o \
	vfs/vfs_bdev.o \
	vfs/vfs_bio.o \
	vfs/vfs_conf.o \
	vfs/vfs_lookup.o \
	vfs/vfs_mount.o \
	vfs/vfs_vnode.o \
	vfs/vfs_task.o \
	vfs/vfs_syscalls.o \
	vfs/vfs_fops.o \
	vfs/vfs_dentry.o

fs_objs += ramfs/ramfs_vfsops.o \
	ramfs/ramfs_vnops.o

fs_objs += devfs/devfs_vnops.o \
	devfs/device.o

fs_objs += rofs/rofs_vfsops.o \
	rofs/rofs_vnops.o \
	rofs/rofs_cache.o \
	rofs/rofs_common.o

ifeq ($(conf_drivers_virtio),1)
fs_objs += virtiofs/virtiofs_vfsops.o \
	virtiofs/virtiofs_vnops.o \
	virtiofs/virtiofs_dax.o
endif

fs_objs += pseudofs/pseudofs.o
fs_objs += procfs/procfs_vnops.o
fs_objs += sysfs/sysfs_vnops.o
fs_objs += zfs/zfs_null_vfsops.o

objects += $(addprefix fs/, $(fs_objs))
objects += $(addprefix libc/, $(libc))
objects += $(addprefix musl/src/, $(musl))

libc_objects_to_hide = $(addprefix $(out)/libc/, $(libc_to_hide))
$(libc_objects_to_hide): cc-hide-flags = $(cc-hide-flags-$(conf_hide_symbols))
$(libc_objects_to_hide): cxx-hide-flags = $(cxx-hide-flags-$(conf_hide_symbols))

libstdc++.a := $(shell $(CXX) -print-file-name=libstdc++.a)
ifeq ($(filter /%,$(libstdc++.a)),)
ifeq ($(arch),aarch64)
    libstdc++.a := $(shell find $(aarch64_gccbase)/ -name libstdc++.a)
    ifeq ($(libstdc++.a),)
        $(error Error: libstdc++.a needs to be installed.)
    endif
else
    $(error Error: libstdc++.a needs to be installed.)
endif
endif

libgcc.a := $(shell $(CC) -print-libgcc-file-name)
ifeq ($(filter /%,$(libgcc.a)),)
ifeq ($(arch),aarch64)
    libgcc.a := $(shell find $(aarch64_gccbase)/ -name libgcc.a |  grep -v /32/)
    ifeq ($(libgcc.a),)
        $(error Error: libgcc.a needs to be installed.)
    endif
else
    $(error Error: libgcc.a needs to be installed.)
endif
endif

libgcc_eh.a := $(shell $(CC) -print-file-name=libgcc_eh.a)
ifeq ($(filter /%,$(libgcc_eh.a)),)
ifeq ($(arch),aarch64)
    libgcc_eh.a := $(shell find $(aarch64_gccbase)/ -name libgcc_eh.a |  grep -v /32/)
    ifeq ($(libgcc_eh.a),)
        $(error Error: libgcc_eh.a needs to be installed.)
    endif
else
    $(error Error: libgcc_eh.a needs to be installed.)
endif
endif

#Allow user specify non-default location of boost
ifeq ($(boost_base),)
    # link with -mt if present, else the base version (and hope it is multithreaded)
    boost-mt := -mt
    boost-lib-dir := $(dir $(shell $(CC) --print-file-name libboost_system$(boost-mt).a))
    ifeq ($(filter /%,$(boost-lib-dir)),)
        boost-mt :=
        boost-lib-dir := $(dir $(shell $(CC) --print-file-name libboost_system$(boost-mt).a))
    endif
    # When boost_env=host, we won't use "-nostdinc", so the build machine's
    # header files will be used normally. So we don't need to add anything
    # special for Boost.
    boost-includes =
    ifeq ($(filter /%,$(boost-lib-dir)),)
        # If the compiler cannot find the boost library, for aarch64 we look in a
        # special location before giving up.
        ifeq ($(arch),aarch64)
            aarch64_boostbase = build/downloaded_packages/aarch64/boost/install
            ifeq (,$(wildcard $(aarch64_boostbase)))
                $(error Missing $(aarch64_boostbase) directory. Please run "./scripts/download_aarch64_packages.py")
            endif

            boost-lib-dir := $(firstword $(dir $(shell find $(aarch64_boostbase)/ -name libboost_system*.a)))
            boost-mt := $(if $(filter %-mt.a, $(wildcard $(boost-lib-dir)/*.a)),-mt)
            boost-includes = -isystem $(aarch64_boostbase)/usr/include
        else
            $(error Error: libboost_system.a needs to be installed.)
        endif
    endif
else
    # Use boost specified by the user
    boost-lib-dir := $(firstword $(dir $(shell find $(boost_base)/ -name libboost_system*.a)))
    boost-mt := $(if $(filter %-mt.a, $(wildcard $(boost-lib-dir)/*.a)),-mt)
    boost-includes = -isystem $(boost_base)/usr/include
endif

boost-libs := $(boost-lib-dir)/libboost_system$(boost-mt).a

objects += fs/nfs/nfs_null_vfsops.o

# The OSv kernel is linked into an ordinary, non-PIE, executable, so there is no point in compiling
# with -fPIC or -fpie and objects that can be linked into a PIE. On the contrary, PIE-compatible objects
# have overheads and can cause problems (see issue #1112). Recently, on some systems gcc's
# default was changed to use -fpie, so we need to undo this default by explicitly specifying -fno-pie.
$(objects:%=$(out)/%) $(drivers:%=$(out)/%) $(out)/arch/$(arch)/boot.o $(out)/loader.o $(out)/runtime.o: COMMON += -fno-pie

# ld has a known bug (https://sourceware.org/bugzilla/show_bug.cgi?id=6468)
# where if the executable doesn't use shared libraries, its .dynamic section
# is dropped, even when we use the "--export-dynamic" (which is silently
# ignored). The workaround is to link loader.elf with a do-nothing library.
$(out)/dummy-shlib.so: $(out)/dummy-shlib.o
	$(call quiet, $(CXX) -nodefaultlibs -shared $(gcc-sysroot) -o $@ $^, LINK $@)

stage1_targets = $(out)/arch/$(arch)/boot.o $(out)/loader.o $(out)/runtime.o $(drivers:%=$(out)/%) $(objects:%=$(out)/%) $(out)/dummy-shlib.so
stage1: $(stage1_targets) links $(out)/default_version_script
.PHONY: stage1

loader_options_dep = $(out)/arch/$(arch)/loader_options.ld
$(loader_options_dep): stage1
	$(makedir)
	@if [ '$(shell cat $(loader_options_dep) 2>&1)' != 'APP_LOCAL_EXEC_TLS_SIZE = $(app_local_exec_tls_size);' ]; then \
		echo -n "APP_LOCAL_EXEC_TLS_SIZE = $(app_local_exec_tls_size);" > $(loader_options_dep) ; \
	fi

ifeq ($(conf_hide_symbols),1)
version_script_file:=$(out)/version_script
#Detect which version script to be used and copy to $(out)/version_script
#so that loader.elf/kernel.elf is rebuilt accordingly if version script has changed
ifdef conf_version_script
ifeq (,$(wildcard $(conf_version_script)))
    $(error Missing version script: $(conf_version_script))
endif
ifneq ($(shell cmp $(out)/version_script $(conf_version_script)),)
$(shell cp $(conf_version_script) $(out)/version_script)
endif
else
ifeq ($(shell test -e $(out)/version_script || echo -n no),no)
$(shell cp $(out)/default_version_script $(out)/version_script)
endif
ifneq ($(shell cmp $(out)/version_script $(out)/default_version_script),)
$(shell cp $(out)/default_version_script $(out)/version_script)
endif
endif
linker_archives_options = --no-whole-archive $(libstdc++.a) $(libgcc.a) $(libgcc_eh.a) $(boost-libs) \
  --exclude-libs libstdc++.a --gc-sections
else
linker_archives_options = --whole-archive $(libstdc++.a) $(libgcc_eh.a) $(boost-libs) --no-whole-archive $(libgcc.a)
endif

$(out)/default_version_script: exported_symbols/*.symbols exported_symbols/$(arch)/*.symbols
	$(call quiet, scripts/generate_version_script.sh $(out)/default_version_script, GEN default_version_script)

ifeq ($(arch),aarch64)
def_symbols = --defsym=OSV_KERNEL_VM_BASE=$(kernel_vm_base)
else
def_symbols = --defsym=OSV_KERNEL_BASE=$(kernel_base) \
              --defsym=OSV_KERNEL_VM_BASE=$(kernel_vm_base) \
              --defsym=OSV_KERNEL_VM_SHIFT=$(kernel_vm_shift)
endif

$(out)/loader.elf: $(stage1_targets) arch/$(arch)/loader.ld $(out)/bootfs.o $(loader_options_dep) $(version_script_file)
	$(call quiet, $(LD) -o $@ $(def_symbols) \
		-Bdynamic --export-dynamic --eh-frame-hdr --enable-new-dtags -L$(out)/arch/$(arch) \
            $(patsubst %version_script,--version-script=%version_script,$(patsubst %.ld,-T %.ld,$^)) \
	    $(linker_archives_options) $(conf_linker_extra_options), \
		LINK loader.elf)
	@# Build libosv.so matching this loader.elf. This is not a separate
	@# rule because that caused bug #545.
	@readelf --dyn-syms --wide $(out)/loader.elf > $(out)/osv.syms
	@scripts/libosv.py $(out)/osv.syms $(out)/libosv.ld `scripts/osv-version.sh` | $(CC) -c -o $(out)/osv.o -x assembler -
	$(call quiet, $(CC) $(out)/osv.o -nostdlib -shared -o $(out)/libosv.so -T $(out)/libosv.ld, LIBOSV.SO)

$(out)/kernel.elf: $(stage1_targets) arch/$(arch)/loader.ld $(out)/empty_bootfs.o $(loader_options_dep) $(version_script_file)
	$(call quiet, $(LD) -o $@ $(def_symbols) \
		-Bdynamic --export-dynamic --eh-frame-hdr --enable-new-dtags -L$(out)/arch/$(arch) \
            $(patsubst %version_script,--version-script=%version_script,$(patsubst %.ld,-T %.ld,$^)) \
	    $(linker_archives_options) $(conf_linker_extra_options), \
		LINK kernel.elf)
	$(call quiet, $(STRIP) $(out)/kernel.elf -o $(out)/kernel-stripped.elf, STRIP kernel.elf -> kernel-stripped.elf )

$(out)/bsd/%.o: COMMON += -DSMP -D'__FBSDID(__str__)=extern int __bogus__'

environ_sources = $(addprefix libc/, $(environ_libc))
environ_sources += $(addprefix musl/src/, $(environ_musl))

$(out)/libenviron.so: pre-include-api = -isystem include/api/internal_musl_headers -isystem musl/src/include
$(out)/libenviron.so: source-dialects =

$(out)/libenviron.so: $(environ_sources)
	$(makedir)
	 $(call quiet, $(CC) $(CFLAGS) -shared -o $(out)/libenviron.so $(environ_sources), CC libenviron.so)

$(out)/libvdso.so: libc/vdso/vdso.c
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -c -fPIC -o $(out)/libvdso.o libc/vdso/vdso.c, CC libvdso.o)
	$(call quiet, $(LD) -shared -fPIC -o $(out)/libvdso.so $(out)/libvdso.o --version-script=libc/vdso/vdso.version, LINK libvdso.so)

bootfs_manifest ?= bootfs.manifest.skel

# If parameter "bootfs_manifest" has been changed since the last make,
# bootfs.bin requires rebuilding
bootfs_manifest_dep = $(out)/bootfs_manifest.last
.PHONY: phony
$(bootfs_manifest_dep): phony
	@if [ '$(shell cat $(bootfs_manifest_dep) 2>&1)' != '$(bootfs_manifest)' ]; then \
		echo -n $(bootfs_manifest) > $(bootfs_manifest_dep) ; \
	fi

libgcc_s_dir := $(dir $(shell $(CC) -print-file-name=libgcc_s.so.1))
ifeq ($(filter /%,$(libgcc_s_dir)),)
libgcc_s_dir := ../../$(aarch64_gccbase)/lib64
endif

$(out)/bootfs.bin: scripts/mkbootfs.py $(bootfs_manifest) $(bootfs_manifest_dep) $(tools:%=$(out)/%) \
		$(out)/zpool.so $(out)/zfs.so $(out)/libenviron.so $(out)/libvdso.so
	$(call quiet, olddir=`pwd`; cd $(out); "$$olddir"/scripts/mkbootfs.py -o bootfs.bin -d bootfs.bin.d -m "$$olddir"/$(bootfs_manifest) \
		-D libgcc_s_dir=$(libgcc_s_dir), MKBOOTFS $@)

$(out)/bootfs.o: $(out)/bootfs.bin
$(out)/bootfs.o: ASFLAGS += -I$(out)

$(out)/empty_bootfs.o: ASFLAGS += -I$(out)

$(out)/tools/mkfs/mkfs.so: $(out)/tools/mkfs/mkfs.o $(out)/libzfs.so
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -o $@ $(out)/tools/mkfs/mkfs.o -L$(out) -lzfs, LINK mkfs.so)

$(out)/tools/cpiod/cpiod.so: $(out)/tools/cpiod/cpiod.o $(out)/tools/cpiod/cpio.o $(out)/libzfs.so
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -o $@ $(out)/tools/cpiod/cpiod.o $(out)/tools/cpiod/cpio.o -L$(out) -lzfs, LINK cpiod.so)

################################################################################
# The dependencies on header files are automatically generated only after the
# first compilation, as explained above. However, header files generated by
# the Makefile are special, in that they need to be created even *before* the
# first compilation. Moreover, some (namely version.h) need to perhaps be
# re-created on every compilation. "generated-headers" is used as an order-
# only dependency on C compilation rules above, so we don't try to compile
# C code before generating these headers.
generated-headers: $(out)/gen/include/bits/alltypes.h perhaps-modify-version-h perhaps-modify-drivers-config-h
.PHONY: generated-headers

# While other generated headers only need to be generated once, version.h
# should be recreated on every compilation. To avoid a cascade of
# recompilation, the rule below makes sure not to modify version.h's timestamp
# if the version hasn't changed.
perhaps-modify-version-h:
	$(call quiet, sh scripts/gen-version-header $(out)/gen/include/osv/version.h, GEN gen/include/osv/version.h)
.PHONY: perhaps-modify-version-h

# Using 'if ($(conf_drivers_*),1)' in the rules below is enough to include whole object
# files. Sometimes though we need to enable or disable portions of the code specific
# to given driver (the arch-setup.cc is best example). To that end the rule below
# generates drivers_config.h header file with the macros CONF_drivers_* which is
# then included by relevant source files.
# This allows for fairly rapid rebuilding of the kernel for specified profiles
# as only few files need to be re-compiled.
perhaps-modify-drivers-config-h:
	$(call quiet, sh scripts/gen-drivers-config-header $(arch) $(out)/gen/include/osv/drivers_config.h, GEN gen/include/osv/drivers_config.h)
.PHONY: perhaps-modify-drivers-config-h

$(out)/gen/include/bits/alltypes.h: include/api/$(arch)/bits/alltypes.h.sh
	$(makedir)
	$(call quiet, sh $^ > $@, GEN $@)

# The generated header ctype-data.h is different in that it is only included
# at one place (runtime.c), so instead of making it a dependency of
# generated-headers, we can just make it a dependency of runtime.o
$(out)/runtime.o: $(out)/gen/include/ctype-data.h

$(out)/gen/include/ctype-data.h: $(out)/gen-ctype-data
	$(makedir)
	$(call quiet, $(out)/gen-ctype-data > $@, GEN $@)

$(out)/gen-ctype-data: gen-ctype-data.cc
	$(call quiet, $(HOST_CXX) -o $@ $^, HOST_CXX $@)

################################################################################




#include $(src)/bsd/cddl/contrib/opensolaris/lib/libuutil/common/build.mk:
libuutil-file-list = uu_alloc uu_avl uu_dprintf uu_ident uu_list uu_misc uu_open uu_pname uu_string uu_strtoint
libuutil-objects = $(foreach file, $(libuutil-file-list), $(out)/bsd/cddl/contrib/opensolaris/lib/libuutil/common/$(file).o)

define libuutil-includes
  bsd/cddl/contrib/opensolaris/lib/libuutil/common
  bsd/cddl/compat/opensolaris/include
  bsd/sys/cddl/contrib/opensolaris/uts/common
  bsd/sys/cddl/compat/opensolaris
  bsd/cddl/contrib/opensolaris/head
  bsd/include
endef

cflags-libuutil-include = $(foreach path, $(strip $(libuutil-includes)), -isystem $(path))

$(libuutil-objects): local-includes += $(cflags-libuutil-include)

# disable the main bsd include search order, we want it before osv but after solaris
$(libuutil-objects): post-includes-bsd =

$(libuutil-objects): kernel-defines =

$(libuutil-objects): CFLAGS += -Wno-unknown-pragmas

$(out)/libuutil.so: $(libuutil-objects)
	$(makedir)
	$(q-build-so)

#include $(src)/bsd/cddl/contrib/opensolaris/lib/libzfs/common/build.mk:

libzfs-file-list = changelist config dataset diff import iter mount pool status util
libzfs-objects = $(foreach file, $(libzfs-file-list), $(out)/bsd/cddl/contrib/opensolaris/lib/libzfs/common/libzfs_$(file).o)

libzpool-file-list = util kernel
libzpool-objects = $(foreach file, $(libzpool-file-list), $(out)/bsd/cddl/contrib/opensolaris/lib/libzpool/common/$(file).o)

libsolaris-objects = $(foreach file, $(solaris) $(xdr), $(out)/$(file))
libsolaris-objects += $(out)/bsd/porting/kobj.o $(out)/fs/zfs/zfs_initialize.o

$(libsolaris-objects): kernel-defines = -D_KERNEL $(source-dialects) -fvisibility=hidden -ffunction-sections -fdata-sections

$(out)/fs/zfs/zfs_initialize.o: CFLAGS+= \
	-DBUILDING_ZFS \
	-Ibsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs \
	-Ibsd/sys/cddl/contrib/opensolaris/common/zfs \
	-Ibsd/sys/cddl/compat/opensolaris \
	-Ibsd/sys/cddl/contrib/opensolaris/common \
	-Ibsd/sys/cddl/contrib/opensolaris/uts/common \
	-Ibsd/sys \
	-Wno-array-bounds \
	-fno-strict-aliasing \
	-Wno-unknown-pragmas \
	-Wno-unused-variable \
	-Wno-switch \
	-Wno-maybe-uninitialized

#build libsolaris.so with -z,now so that all symbols get resolved eagerly (BIND_NOW)
#also make sure libsolaris.so has osv-mlock note (see zfs_initialize.c) so that
# the file segments get loaded eagerly as well when mmapped
comma:=,
$(out)/libsolaris.so: $(libsolaris-objects)
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -Wl$(comma)-z$(comma)now -Wl$(comma)--gc-sections -o $@ $(libsolaris-objects) -L$(out), LINK libsolaris.so)

libzfs-objects += $(libzpool-objects)
libzfs-objects += $(out)/bsd/cddl/compat/opensolaris/misc/mkdirp.o
libzfs-objects += $(out)/bsd/cddl/compat/opensolaris/misc/zmount.o
libzfs-objects += $(out)/bsd/cddl/contrib/opensolaris/lib/libzfs/common/zfs_prop.o
libzfs-objects += $(out)/bsd/cddl/contrib/opensolaris/lib/libzfs/common/zprop_common.o

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

cflags-libzfs-include = $(foreach path, $(strip $(libzfs-includes)), -isystem $(path))

$(libzfs-objects): local-includes += $(cflags-libzfs-include)

# disable the main bsd include search order, we want it before osv but after solaris
$(libzfs-objects): post-includes-bsd =

$(libzfs-objects): kernel-defines =

$(libzfs-objects): CFLAGS += -D_GNU_SOURCE

$(libzfs-objects): CFLAGS += -Wno-switch -D__va_list=__builtin_va_list '-DTEXT_DOMAIN=""' \
			-Wno-maybe-uninitialized -Wno-unused-variable -Wno-unknown-pragmas -Wno-unused-function \
			-D_OPENSOLARIS_SYS_UIO_H_

$(out)/bsd/cddl/contrib/opensolaris/lib/libzpool/common/kernel.o: CFLAGS += -fvisibility=hidden
$(out)/bsd/cddl/contrib/opensolaris/lib/libzfs/common/zfs_prop.o: CFLAGS += -fvisibility=hidden

# Note: zfs_prop.c and zprop_common.c are also used by the kernel, thus the manual targets.
$(out)/bsd/cddl/contrib/opensolaris/lib/libzfs/common/zfs_prop.o: bsd/sys/cddl/contrib/opensolaris/common/zfs/zfs_prop.c | generated-headers
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -c -o $@ $<, CC $<)

$(out)/bsd/cddl/contrib/opensolaris/lib/libzfs/common/zprop_common.o: bsd/sys/cddl/contrib/opensolaris/common/zfs/zprop_common.c | generated-headers
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -c -o $@ $<, CC $<)

$(out)/libzfs.so: $(libzfs-objects) $(out)/libuutil.so $(out)/libsolaris.so
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -o $@ $(libzfs-objects) -L$(out) -luutil -lsolaris, LINK libzfs.so)

#include $(src)/bsd/cddl/contrib/opensolaris/cmd/zpool/build.mk:
zpool-cmd-file-list = zpool_iter  zpool_main  zpool_util  zpool_vdev

zpool-cmd-objects = $(foreach x, $(zpool-cmd-file-list), $(out)/bsd/cddl/contrib/opensolaris/cmd/zpool/$x.o)
zpool-cmd-objects += $(out)/bsd/porting/mnttab.o

cflags-zpool-cmd-includes = $(cflags-libzfs-include) -Ibsd/cddl/contrib/opensolaris/cmd/stat/common

$(zpool-cmd-objects): kernel-defines =

$(zpool-cmd-objects): CFLAGS += -D_GNU_SOURCE

$(zpool-cmd-objects): local-includes += $(cflags-zpool-cmd-includes)

$(zpool-cmd-objects): CFLAGS += -Wno-switch -D__va_list=__builtin_va_list '-DTEXT_DOMAIN=""' \
			-Wno-maybe-uninitialized -Wno-unused-variable -Wno-unknown-pragmas -Wno-unused-function


$(out)/zpool.so: $(zpool-cmd-objects) $(out)/libzfs.so
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -o $@ $(zpool-cmd-objects) -L$(out) -lzfs, LINK zpool.so)

#include $(src)/bsd/cddl/contrib/opensolaris/cmd/zfs/build.mk:
zfs-cmd-file-list = zfs_iter zfs_main

zfs-cmd-objects = $(foreach x, $(zfs-cmd-file-list), $(out)/bsd/cddl/contrib/opensolaris/cmd/zfs/$x.o)
zfs-cmd-objects += $(out)/bsd/porting/mnttab.o

cflags-zfs-cmd-includes = $(cflags-libzfs-include)

$(zfs-cmd-objects): kernel-defines =

$(zfs-cmd-objects): CFLAGS += -D_GNU_SOURCE

$(zfs-cmd-objects): local-includes += $(cflags-zfs-cmd-includes)

$(zfs-cmd-objects): CFLAGS += -Wno-switch -D__va_list=__builtin_va_list '-DTEXT_DOMAIN=""' \
			-Wno-maybe-uninitialized -Wno-unused-variable -Wno-unknown-pragmas -Wno-unused-function


$(out)/zfs.so: $(zfs-cmd-objects) $(out)/libzfs.so
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -o $@ $(zfs-cmd-objects) -L$(out) -lzfs, LINK zfs.so)
