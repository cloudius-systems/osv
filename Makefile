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
ifdef image
#$(error Please use scripts/build to build images)
$(info "make image=..." deprecated. Please use "scripts/build image=...".)
endif
ifdef modules
#$(error Please use scripts/build to build images)
$(info "make modules=..." deprecated. Please use "scripts/build modules=...".)
endif

# Ugly hack to support the old "make ... image=..." image building syntax, and
# pass it into scripts/build. We should eventually get rid of this, and turn
# the above deprecated messages into errors.
ugly_backward_compatibility_hack: all
	@test -n "$(image)" &&  ./scripts/build image=$(image) || :
	@test -n "$(modules)" &&  ./scripts/build modules=$(modules) || :

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

CROSS_PREFIX ?= $(if $(filter-out $(arch), $(host_arch)), $(arch)-linux-gnu-)
CXX=$(CROSS_PREFIX)g++
CC=$(CROSS_PREFIX)gcc
LD=$(CROSS_PREFIX)ld
STRIP=$(CROSS_PREFIX)strip
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


# This makefile wraps all commands with the $(quiet) or $(very-quiet) macros
# so that instead of half-a-screen-long command lines we short summaries
# like "CC file.cc". These macros also keep the option of viewing the
# full command lines, if you wish, with "make V=1".
quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)

# TODO: These java-targets shouldn't be compiled here, but rather in modules/java/Makefile.
# The problem is that getting the right compilation lines there is hard :-(
ifeq ($(arch),aarch64)
java-targets :=
else
java-targets := $(out)/java/jvm/java.so $(out)/java/jni/balloon.so $(out)/java/jni/elf-loader.so $(out)/java/jni/networking.so \
        $(out)/java/jni/stty.so $(out)/java/jni/tracepoint.so $(out)/java/jni/power.so $(out)/java/jni/monitor.so
endif

all: $(out)/loader.img $(java-targets)
	$(call very-quiet, ln -nsf $(notdir $(out)) $(outlink))
	$(call very-quiet, ln -nsf $(notdir $(out)) $(outlink2))
.PHONY: all

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


# The user can override the build_env variable (or one or more of *_env
# variables below) to decide if to take the host's C/C++ libraries, or
# those from the external/ directory.
build_env ?= $(if $(filter $(host_arch), $(arch)),host,external)
ifeq ($(build_env), host)
    gcc_lib_env ?= host
    cxx_lib_env ?= host
    gcc_include_env ?= host
    boost_env ?= host
else
    gcc_lib_env ?= external
    cxx_lib_env ?= external
    gcc_include_env ?= external
    boost_env ?= external
endif


local-includes =
INCLUDES = $(local-includes) -Iarch/$(arch) -I. -Iinclude  -Iarch/common
INCLUDES += -isystem include/glibc-compat

glibcbase = external/$(arch)/glibc.bin
gccbase = external/$(arch)/gcc.bin
miscbase = external/$(arch)/misc.bin
jdkbase := $(shell find external/$(arch)/openjdk.bin/usr/lib/jvm \
                         -maxdepth 1 -type d -name 'java*')


ifeq ($(gcc_include_env), external)
  gcc-inc-base := $(dir $(shell find $(gccbase)/ -name vector | grep -v -e debug/vector$$ -e profile/vector$$))
  gcc-inc-base3 := $(dir $(shell dirname `find $(gccbase)/ -name c++config.h | grep -v /32/`))
  INCLUDES += -isystem $(gcc-inc-base)
  INCLUDES += -isystem $(gcc-inc-base3)
endif

ifeq ($(arch),x64)
INCLUDES += -isystem external/$(arch)/acpica/source/include
endif

ifeq ($(arch),aarch64)
libfdt_base = external/$(arch)/libfdt
INCLUDES += -isystem $(libfdt_base)
endif

INCLUDES += $(boost-includes)
INCLUDES += -isystem include/api
INCLUDES += -isystem include/api/$(arch)
ifeq ($(gcc_include_env), external)
  gcc-inc-base2 := $(dir $(shell find $(gccbase)/ -name unwind.h))
  # must be after include/api, since it includes some libc-style headers:
  INCLUDES += -isystem $(gcc-inc-base2)
endif
INCLUDES += -isystem $(out)/gen/include
INCLUDES += $(post-includes-bsd)

post-includes-bsd += -isystem bsd/sys
# For acessing machine/ in cpp xen drivers
post-includes-bsd += -isystem bsd/
post-includes-bsd += -isystem bsd/$(arch)

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

kernel-defines = -D_KERNEL $(source-dialects)

gcc-sysroot = $(if $(CROSS_PREFIX), --sysroot external/$(arch)/gcc.bin) \

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
EXTRA_FLAGS = -D__OSV_CORE__ -DOSV_KERNEL_BASE=$(kernel_base)
EXTRA_LIBS =
COMMON = $(autodepend) -g -Wall -Wno-pointer-arith $(CFLAGS_WERROR) -Wformat=0 -Wno-format-security \
	-D __BSD_VISIBLE=1 -U _FORTIFY_SOURCE -fno-stack-protector $(INCLUDES) \
	$(kernel-defines) \
	-fno-omit-frame-pointer $(compiler-specific) \
	-include compiler/include/intrinsics.hh \
	$(do-sys-includes) \
	$(arch-cflags) $(conf-opt) $(acpi-defines) $(tracing-flags) $(gcc-sysroot) \
	$(configuration) -D__OSV__ -D__XEN_INTERFACE_VERSION__="0x00030207" -DARCH_STRING=$(ARCH_STR) $(EXTRA_FLAGS)
ifeq ($(gcc_include_env), external)
ifeq ($(boost_env), external)
  COMMON += -nostdinc
endif
endif

tracing-flags-0 =
tracing-flags-1 = -finstrument-functions -finstrument-functions-exclude-file-list=c++,trace.cc,trace.hh,align.hh,mmintrin.h
tracing-flags = $(tracing-flags-$(conf-tracing))

gcc-opt-Og := $(call compiler-flag, -Og, -Og, compiler/empty.cc)

CXXFLAGS = -std=gnu++11 $(COMMON)
CFLAGS = -std=gnu99 $(COMMON)

# should be limited to files under libc/ eventually
CFLAGS += -I libc/stdio -I libc/internal -I libc/arch/$(arch) \
	-Wno-missing-braces -Wno-parentheses -Wno-unused-but-set-variable

ASFLAGS = -g $(autodepend) -DASSEMBLY

$(out)/fs/vfs/main.o: CXXFLAGS += -Wno-sign-compare -Wno-write-strings

$(out)/bsd/%.o: INCLUDES += -isystem bsd/sys
$(out)/bsd/%.o: INCLUDES += -isystem bsd/
# for machine/
$(out)/bsd/%.o: INCLUDES += -isystem bsd/$(arch)

configuration-defines = conf-preempt conf-debug_memory conf-logger_debug

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
	$(call quiet, $(CXX) $(CXXFLAGS) $(ASFLAGS) -c -o $@ $<, AS $*.s)

$(out)/%.o: %.s
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) $(ASFLAGS) -c -o $@ $<, AS $*.s)

%.so: EXTRA_FLAGS = -fPIC -shared
%.so: %.o
	$(makedir)
	$(q-build-so)

sys-includes = $(jdkbase)/include $(jdkbase)/include/linux
autodepend = -MD -MT $@ -MP

do-sys-includes = $(foreach inc, $(sys-includes), -isystem $(inc))

tools := tools/mkfs/mkfs.so tools/cpiod/cpiod.so

$(out)/tools/%.o: COMMON += -fPIC

# TODO: The "ifconfig" and "lsroute" programs are only needed for the mgmt
# module... Better move it out of the OSv core...
tools += tools/ifconfig/ifconfig.so
tools += tools/route/lsroute.so
$(out)/tools/route/lsroute.so: EXTRA_LIBS = -L$(out)/tools/ -ltools
$(out)/tools/route/lsroute.so: $(out)/tools/libtools.so
$(out)/tools/ifconfig/ifconfig.so: EXTRA_LIBS = -L$(out)/tools/ -ltools
$(out)/tools/ifconfig/ifconfig.so: $(out)/tools/libtools.so

tools += tools/uush/uush.so
tools += tools/uush/ls.so
tools += tools/uush/mkdir.so

# TODO: we only need this libtools for the httpserver module... Better
# move it to its own module, it shouldn't be in the OSv core...
tools += tools/libtools.so

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

kernel_base := 0x200000

$(out)/boot.bin: arch/x64/boot16.ld $(out)/arch/x64/boot16.o
	$(call quiet, $(LD) -o $@ -T $^, LD $@)

image-size = $(shell stat --printf %s $(out)/lzloader.elf)

$(out)/loader.img: $(out)/boot.bin $(out)/lzloader.elf
	$(call quiet, dd if=$(out)/boot.bin of=$@ > /dev/null 2>&1, DD loader.img boot.bin)
	$(call quiet, dd if=$(out)/lzloader.elf of=$@ conv=notrunc seek=128 > /dev/null 2>&1, \
		DD loader.img lzloader.elf)
	$(call quiet, scripts/imgedit.py setsize $@ $(image-size), IMGEDIT $@)
	$(call quiet, scripts/imgedit.py setargs $@ $(cmdline), IMGEDIT $@)

$(out)/loader.bin: $(out)/arch/x64/boot32.o arch/x64/loader32.ld
	$(call quiet, $(LD) -nostartfiles -static -nodefaultlibs -o $@ \
	                $(filter-out %.bin, $(^:%.ld=-T %.ld)), LD $@)

$(out)/arch/x64/boot32.o: $(out)/loader.elf

$(out)/fastlz/fastlz.o:
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) -O2 -m32 -fno-instrument-functions -o $@ -c fastlz/fastlz.cc, CXX fastlz/fastlz.cc)

$(out)/fastlz/lz: fastlz/fastlz.cc fastlz/lz.cc
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) -O2 -o $@ $(filter %.cc, $^), CXX $@)

$(out)/loader-stripped.elf.lz.o: $(out)/loader-stripped.elf $(out)/fastlz/lz
	$(call quiet, $(out)/fastlz/lz $(out)/loader-stripped.elf, LZ loader-stripped.elf)
	$(call quiet, cd $(out); objcopy -B i386 -I binary -O elf32-i386 loader-stripped.elf.lz loader-stripped.elf.lz.o, OBJCOPY loader-stripped.elf.lz -> loader-stripped.elf.lz.o)

$(out)/fastlz/lzloader.o: fastlz/lzloader.cc
	$(call quiet, $(CXX) $(CXXFLAGS) -O2 -m32 -fno-instrument-functions -o $@ -c fastlz/lzloader.cc, CXX $<)

$(out)/lzloader.elf: $(out)/loader-stripped.elf.lz.o $(out)/fastlz/lzloader.o arch/x64/lzloader.ld \
	$(out)/fastlz/fastlz.o
	$(call very-quiet, scripts/check-image-size.sh $(out)/loader-stripped.elf 23068672)
	$(call quiet, $(LD) -o $@ \
		-Bdynamic --export-dynamic --eh-frame-hdr --enable-new-dtags \
		-T arch/x64/lzloader.ld \
		$(filter %.o, $^), LINK lzloader.elf)

acpi-defines = -DACPI_MACHINE_WIDTH=64 -DACPI_USE_LOCAL_CACHE

acpi-source := $(shell find external/$(arch)/acpica/source/components -type f -name '*.c')
acpi = $(patsubst %.c, %.o, $(acpi-source))

$(acpi:%=$(out)/%): CFLAGS += -fno-strict-aliasing -Wno-strict-aliasing

endif # x64

ifeq ($(arch),aarch64)

kernel_base := 0x40080000

include $(libfdt_base)/Makefile.libfdt
libfdt-source := $(patsubst %.c, $(libfdt_base)/%.c, $(LIBFDT_SRCS))
libfdt = $(patsubst %.c, %.o, $(libfdt-source))

$(out)/preboot.elf: arch/$(arch)/preboot.ld $(out)/arch/$(arch)/preboot.o
	$(call quiet, $(LD) -o $@ -T $^, LD $@)

$(out)/preboot.bin: $(out)/preboot.elf
	$(call quiet, $(OBJCOPY) -O binary $^ $@, OBJCOPY $@)

#image-size = $(shell stat --printf %s $(out)/loader-stripped.elf)

$(out)/loader.img: $(out)/preboot.bin $(out)/loader-stripped.elf
	$(call quiet, dd if=$(out)/preboot.bin of=$@ > /dev/null 2>&1, DD $@ preboot.bin)
	$(call quiet, dd if=$(out)/loader-stripped.elf of=$@ conv=notrunc obs=4096 seek=16 > /dev/null 2>&1, DD $@ loader-stripped.elf)
	$(call quiet, scripts/imgedit.py setargs $@ $(cmdline), IMGEDIT $@)

endif # aarch64

$(out)/bsd/sys/crypto/sha2/sha2.o: CFLAGS+=-Wno-strict-aliasing
$(out)/bsd/sys/crypto/rijndael/rijndael-api-fst.o: CFLAGS+=-Wno-strict-aliasing


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
bsd += bsd/porting/kobj.o
bsd += bsd/sys/netinet/if_ether.o  
bsd += bsd/sys/compat/linux/linux_socket.o  
bsd += bsd/sys/compat/linux/linux_ioctl.o  
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
bsd += bsd/sys/xdr/xdr.o
bsd += bsd/sys/xdr/xdr_array.o
bsd += bsd/sys/xdr/xdr_mem.o

ifeq ($(arch),x64)
$(out)/bsd/%.o: COMMON += -DXEN -DXENHVM
bsd += bsd/sys/xen/gnttab.o
bsd += bsd/sys/xen/evtchn.o
bsd += bsd/sys/xen/xenstore/xenstore.o
bsd += bsd/sys/xen/xenbus/xenbus.o
bsd += bsd/sys/xen/xenbus/xenbusb.o
bsd += bsd/sys/xen/xenbus/xenbusb_front.o
bsd += bsd/sys/dev/xen/netfront/netfront.o
bsd += bsd/sys/dev/xen/blkfront/blkfront.o
endif

bsd += bsd/sys/dev/random/hash.o
bsd += bsd/sys/dev/random/randomdev_soft.o
bsd += bsd/sys/dev/random/yarrow.o
bsd += bsd/sys/dev/random/random_harvestq.o
bsd += bsd/sys/dev/random/harvest.o
bsd += bsd/sys/dev/random/live_entropy_sources.o

$(out)/bsd/sys/%.o: COMMON += -Wno-sign-compare -Wno-narrowing -Wno-write-strings -Wno-parentheses -Wno-unused-but-set-variable

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
	-Wno-strict-aliasing \
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

drivers := $(bsd) $(solaris)
drivers += core/mmu.o
drivers += arch/$(arch)/early-console.o
drivers += drivers/console.o
drivers += drivers/console-multiplexer.o
drivers += drivers/console-driver.o
drivers += drivers/line-discipline.o
drivers += drivers/clock.o
drivers += drivers/clock-common.o
drivers += drivers/clockevent.o
drivers += drivers/ramdisk.o
drivers += core/elf.o
drivers += java/jvm/jvm_balloon.o
drivers += java/jvm/java_api.o
drivers += java/jvm/jni_helpers.o
drivers += drivers/random.o
drivers += drivers/zfs.o
drivers += drivers/null.o
drivers += drivers/device.o
drivers += drivers/pci-generic.o
drivers += drivers/pci-device.o
drivers += drivers/pci-function.o
drivers += drivers/pci-bridge.o
drivers += drivers/driver.o

ifeq ($(arch),x64)
drivers += $(libtsm)
drivers += drivers/vga.o drivers/kbd.o drivers/isa-serial.o
drivers += arch/$(arch)/pvclock-abi.o
drivers += drivers/virtio.o
drivers += drivers/virtio-vring.o
drivers += drivers/virtio-net.o
drivers += drivers/virtio-assign.o
drivers += drivers/vmxnet3.o
drivers += drivers/vmxnet3-queues.o
drivers += drivers/virtio-blk.o
drivers += drivers/virtio-scsi.o
drivers += drivers/virtio-rng.o
drivers += drivers/kvmclock.o drivers/xenclock.o
drivers += drivers/acpi.o
drivers += drivers/hpet.o
drivers += drivers/xenfront.o drivers/xenfront-xenbus.o drivers/xenfront-blk.o
drivers += drivers/pvpanic.o
drivers += drivers/ahci.o
drivers += drivers/ide.o
drivers += drivers/scsi-common.o
drivers += drivers/vmw-pvscsi.o
endif # x64

ifeq ($(arch),aarch64)
drivers += drivers/pl011.o
drivers += drivers/virtio.o
drivers += drivers/virtio-vring.o
drivers += drivers/virtio-rng.o
drivers += drivers/virtio-blk.o
drivers += drivers/virtio-net.o
endif # aarch64

objects := bootfs.o
objects += arch/$(arch)/arch-trace.o
objects += arch/$(arch)/arch-setup.o
objects += arch/$(arch)/signal.o
objects += arch/$(arch)/string.o
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
objects += arch/$(arch)/pci.o
objects += arch/$(arch)/msi.o
objects += arch/$(arch)/power.o

$(out)/arch/x64/string-ssse3.o: CXXFLAGS += -mssse3

ifeq ($(arch),aarch64)
objects += arch/$(arch)/psci.o
objects += arch/$(arch)/arm-clock.o
objects += arch/$(arch)/gic.o
objects += arch/$(arch)/arch-dtb.o
objects += $(libfdt)
endif

ifeq ($(arch),x64)
objects += arch/x64/dmi.o
objects += arch/x64/string-ssse3.o
objects += arch/x64/arch-trace.o
objects += arch/x64/ioapic.o
objects += arch/x64/apic.o
objects += arch/x64/apic-clock.o
objects += arch/x64/entry-xen.o
objects += arch/x64/xen.o
objects += arch/x64/xen_intr.o
objects += core/sampler.o
objects += $(acpi)
endif # x64

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

#include $(src)/libc/build.mk:
libc =
musl =

ifeq ($(arch),x64)
musl_arch = x86_64
else
musl_arch = notsup
endif

libc += internal/_chk_fail.o
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

musl += ctype/__ctype_b_loc.o

musl += errno/strerror.o
libc += errno/strerror.o

musl += locale/catclose.o
musl += locale/catgets.o
musl += locale/catopen.o
libc += locale/duplocale.o
libc += locale/freelocale.o
musl += locale/iconv.o
musl += locale/intl.o
libc += locale/isalnum_l.o
libc += locale/isalpha_l.o
libc += locale/isblank_l.o
libc += locale/iscntrl_l.o
libc += locale/isdigit_l.o
libc += locale/isgraph_l.o
libc += locale/islower_l.o
libc += locale/isprint_l.o
libc += locale/ispunct_l.o
libc += locale/isspace_l.o
libc += locale/isupper_l.o
libc += locale/iswalnum_l.o
libc += locale/iswalpha_l.o
libc += locale/iswblank_l.o
libc += locale/iswcntrl_l.o
libc += locale/iswctype_l.o
libc += locale/iswdigit_l.o
libc += locale/iswgraph_l.o
libc += locale/iswlower_l.o
libc += locale/iswprint_l.o
libc += locale/iswpunct_l.o
libc += locale/iswspace_l.o
libc += locale/iswupper_l.o
libc += locale/iswxdigit_l.o
libc += locale/isxdigit_l.o
libc += locale/langinfo.o
musl += locale/localeconv.o
libc += locale/nl_langinfo_l.o
libc += locale/setlocale.o
musl += locale/strcasecmp_l.o
libc += locale/strcoll.o
libc += locale/strcoll_l.o
musl += locale/strerror_l.o
libc += locale/strfmon.o
libc += locale/strftime_l.o
musl += locale/strncasecmp_l.o
libc += locale/strtod_l.o
libc += locale/strtof_l.o
libc += locale/strtold_l.o
libc += locale/strxfrm.o
libc += locale/strxfrm_l.o
libc += locale/tolower_l.o
libc += locale/toupper_l.o
musl += locale/towctrans_l.o
libc += locale/towlower_l.o
libc += locale/towupper_l.o
libc += locale/uselocale.o
libc += locale/wcscoll.o
libc += locale/wcscoll_l.o
libc += locale/wcsftime_l.o
libc += locale/wcsxfrm.o
libc += locale/wcsxfrm_l.o
musl += locale/wctrans_l.o
libc += locale/wctype_l.o

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
musl += math/exp10.o
musl += math/exp10f.o
musl += math/exp10l.o
musl += math/exp2.o
musl += math/exp2f.o
musl += math/exp2l.o
$(out)/musl/src/math/exp2l.o: CFLAGS += -Wno-error=unused-variable
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
libc += math/finite.o
libc += math/finitef.o
libc += math/finitel.o
musl += math/frexp.o
musl += math/frexpf.o
musl += math/frexpl.o
musl += math/hypot.o
musl += math/hypotf.o
musl += math/hypotl.o
musl += math/ilogb.o
musl += math/ilogbf.o
musl += math/ilogbl.o
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
$(out)/musl/src/math/lgamma_r.o: CFLAGS += -Wno-error=maybe-uninitialized
musl += math/lgammaf.o
musl += math/lgammaf_r.o
$(out)/musl/src/math/lgammaf_r.o: CFLAGS += -Wno-error=maybe-uninitialized
musl += math/lgammal.o
$(out)/musl/src/math/lgammal.o: CFLAGS += -Wno-error=maybe-uninitialized
#musl += math/llrint.o
#musl += math/llrintf.o
#musl += math/llrintl.o
musl += math/llround.o
musl += math/llroundf.o
musl += math/llroundl.o
musl += math/log.o
musl += math/log10.o
musl += math/log10f.o
musl += math/log10l.o
musl += math/log1p.o
musl += math/log1pf.o
musl += math/log1pl.o
musl += math/log2.o
musl += math/log2f.o
musl += math/log2l.o
musl += math/logb.o
musl += math/logbf.o
musl += math/logbl.o
musl += math/logf.o
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
#musl += math/nearbyint.o
#musl += math/nearbyintf.o
#musl += math/nearbyintl.o
musl += math/nextafter.o
musl += math/nextafterf.o
musl += math/nextafterl.o
musl += math/nexttoward.o
musl += math/nexttowardf.o
musl += math/nexttowardl.o
musl += math/pow.o
musl += math/powf.o
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

musl += misc/a64l.o
libc += misc/basename.o
musl += misc/dirname.o
libc += misc/ffs.o
musl += misc/get_current_dir_name.o
musl += misc/gethostid.o
musl += misc/getopt.o
musl += misc/getopt_long.o
musl += misc/getsubopt.o
libc += misc/realpath.o
libc += misc/backtrace.o
libc += misc/uname.o
libc += misc/lockf.o
libc += misc/mntent.o
musl += misc/nftw.o
libc += misc/__longjmp_chk.o

musl += multibyte/btowc.o
musl += multibyte/internal.o
musl += multibyte/mblen.o
musl += multibyte/mbrlen.o
musl += multibyte/mbrtowc.o
musl += multibyte/mbsinit.o
musl += multibyte/mbsnrtowcs.o
libc += multibyte/mbsrtowcs.o
musl += multibyte/mbstowcs.o
musl += multibyte/mbtowc.o
musl += multibyte/wcrtomb.o
musl += multibyte/wcsnrtombs.o
musl += multibyte/wcsrtombs.o
musl += multibyte/wcstombs.o
musl += multibyte/wctob.o
musl += multibyte/wctomb.o

$(out)/libc/multibyte/mbsrtowcs.o: CFLAGS += -Imusl/src/multibyte

libc += network/htonl.o
libc += network/htons.o
libc += network/ntohl.o
libc += network/ntohs.o
libc += network/gethostbyname_r.o
musl += network/gethostbyname2_r.o
musl += network/gethostbyaddr_r.o
musl += network/gethostbyaddr.o
libc += network/getaddrinfo.o
musl += network/freeaddrinfo.o
musl += network/in6addr_any.o
musl += network/in6addr_loopback.o
libc += network/getnameinfo.o
libc += network/__dns.o
libc += network/__ipparse.o
libc += network/inet_addr.o
libc += network/inet_aton.o
musl += network/inet_pton.o
libc += network/inet_ntop.o
musl += network/proto.o
libc += network/if_indextoname.o
libc += network/if_nametoindex.o
libc += network/gai_strerror.o
libc += network/h_errno.o
musl += network/getservbyname_r.o
musl += network/getservbyname.o
musl += network/getservbyport_r.o
musl += network/getifaddrs.o
musl += network/if_nameindex.o
musl += network/if_freenameindex.o

musl += prng/rand.o
musl += prng/rand_r.o
libc += prng/random.o
libc += prng/__rand48_step.o
musl += prng/__seed48.o
musl += prng/drand48.o
musl += prng/lcong48.o
musl += prng/lrand48.o
musl += prng/mrand48.o
musl += prng/seed48.o
musl += prng/srand48.o

libc += process/execve.o
libc += process/execle.o
musl += process/execv.o
musl += process/execl.o
libc += process/waitpid.o

libc += arch/$(arch)/setjmp/setjmp.o
libc += arch/$(arch)/setjmp/longjmp.o
libc += arch/$(arch)/setjmp/sigrtmax.o
libc += arch/$(arch)/setjmp/sigrtmin.o
libc += arch/$(arch)/setjmp/siglongjmp.o
libc += arch/$(arch)/setjmp/sigsetjmp.o
libc += arch/$(arch)/setjmp/block.o
ifeq ($(arch),x64)
libc += arch/$(arch)/ucontext/getcontext.o
libc += arch/$(arch)/ucontext/setcontext.o
libc += arch/$(arch)/ucontext/start_context.o
libc += arch/$(arch)/ucontext/ucontext.o
endif

musl += stdio/__fclose_ca.o
libc += stdio/__fdopen.o
musl += stdio/__fmodeflags.o
libc += stdio/__fopen_rb_ca.o
libc += stdio/__fprintf_chk.o
libc += stdio/__lockfile.o
musl += stdio/__overflow.o
libc += stdio/__stdio_close.o
musl += stdio/__stdio_exit.o
libc += stdio/__stdio_read.o
libc += stdio/__stdio_seek.o
libc += stdio/__stdio_write.o
libc += stdio/__stdout_write.o
musl += stdio/__string_read.o
musl += stdio/__toread.o
musl += stdio/__towrite.o
musl += stdio/__uflow.o
libc += stdio/__vfprintf_chk.o
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
libc += stdio/fopen.o
musl += stdio/fprintf.o
libc += stdio/fputc.o
musl += stdio/fputs.o
musl += stdio/fputwc.o
musl += stdio/fputws.o
musl += stdio/fread.o
libc += stdio/__fread_chk.o
libc += stdio/freopen.o
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
musl += stdio/getchar.o
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
musl += stdio/putchar.o
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
musl += stdio/setvbuf.o
musl += stdio/snprintf.o
musl += stdio/sprintf.o
musl += stdio/sscanf.o
libc += stdio/stderr.o
libc += stdio/stdin.o
libc += stdio/stdout.o
musl += stdio/swprintf.o
musl += stdio/swscanf.o
musl += stdio/tempnam.o
libc += stdio/tmpfile.o
libc += stdio/tmpnam.o
musl += stdio/ungetc.o
musl += stdio/ungetwc.o
musl += stdio/vasprintf.o
libc += stdio/vdprintf.o
musl += stdio/vfprintf.o
libc += stdio/vfscanf.o
musl += stdio/vfwprintf.o
libc += stdio/vfwscanf.o
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
libc += stdlib/qsort_r.o
libc += stdlib/strtol.o
libc += stdlib/strtod.o
libc += stdlib/wcstol.o

libc += string/__memcpy_chk.o
musl += string/bcmp.o
musl += string/bcopy.o
musl += string/bzero.o
musl += string/index.o
libc += string/memccpy.o
libc += string/memchr.o
musl += string/memcmp.o
libc += string/memcpy.o
musl += string/memmem.o
libc += string/memmove.o
musl += string/mempcpy.o
musl += string/memrchr.o
libc += string/__memmove_chk.o
libc += string/memset.o
libc += string/__memset_chk.o
libc += string/rawmemchr.o
musl += string/rindex.o
libc += string/stpcpy.o
libc += string/__stpcpy_chk.o
libc += string/stpncpy.o
musl += string/strcasecmp.o
musl += string/strcasestr.o
libc += string/strcat.o
libc += string/__strcat_chk.o
libc += string/strchr.o
libc += string/strchrnul.o
libc += string/strcmp.o
libc += string/strcpy.o
libc += string/__strcpy_chk.o
libc += string/strcspn.o
libc += string/strdup.o
libc += string/strerror_r.o
libc += string/strlcat.o
libc += string/strlcpy.o
libc += string/strlen.o
musl += string/strncasecmp.o
libc += string/strncat.o
libc += string/__strncat_chk.o
libc += string/strncmp.o
libc += string/strncpy.o
libc += string/__strncpy_chk.o
libc += string/__strndup.o
musl += string/strndup.o
musl += string/strnlen.o
libc += string/strpbrk.o
musl += string/strrchr.o
libc += string/strsep.o
libc += string/strsignal.o
libc += string/strspn.o
musl += string/strstr.o
libc += string/strtok.o
libc += string/strtok_r.o
musl += string/strverscmp.o
libc += string/swab.o
libc += string/wcpcpy.o
libc += string/wcpncpy.o
musl += string/wcscasecmp.o
musl += string/wcscasecmp_l.o
libc += string/wcscat.o
musl += string/wcschr.o
musl += string/wcscmp.o
libc += string/wcscpy.o
musl += string/wcscspn.o
musl += string/wcsdup.o
musl += string/wcslen.o
musl += string/wcsncasecmp.o
musl += string/wcsncasecmp_l.o
libc += string/wcsncat.o
musl += string/wcsncmp.o
libc += string/wcsncpy.o
musl += string/wcsnlen.o
musl += string/wcspbrk.o
musl += string/wcsrchr.o
musl += string/wcsspn.o
libc += string/wcsstr.o
libc += string/wcstok.o
musl += string/wcswcs.o
musl += string/wmemchr.o
musl += string/wmemcmp.o
libc += string/wmemcpy.o
musl += string/wmemmove.o
musl += string/wmemset.o

musl += temp/__randname.o
musl += temp/mkdtemp.o
musl += temp/mkstemp.o
musl += temp/mktemp.o
musl += temp/mkostemps.o

libc += time/__asctime.o
libc += time/__time_to_tm.o
libc += time/__tm_to_time.o
musl += time/asctime.o
musl += time/asctime_r.o
musl += time/ctime.o
musl += time/ctime_r.o
musl += time/difftime.o
musl += time/getdate.o
libc += time/gmtime.o
libc += time/gmtime_r.o
libc += time/localtime.o
libc += time/localtime_r.o
libc += time/mktime.o
libc += time/strftime.o
musl += time/strptime.o
musl += time/time.o
libc += time/timegm.o
libc += time/tzset.o
libc += time/wcsftime.o
libc += time/ftime.o # verbatim copy of the file as in 4b15d9f46a2b@musl
$(out)/libc/time/ftime.o: CFLAGS += -Ilibc/include

musl += unistd/sleep.o
musl += unistd/gethostname.o
libc += unistd/sethostname.o
libc += unistd/sync.o
libc += unistd/getpgid.o
libc += unistd/setpgid.o
libc += unistd/getpgrp.o
libc += unistd/getppid.o
libc += unistd/getsid.o
libc += unistd/setsid.o

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
libc += libc.o
libc += dlfcn.o
libc += time.o
libc += signal.o
libc += mman.o
libc += sem.o
libc += pipe_buffer.o
libc += pipe.o
libc += af_local.o
libc += user.o
libc += resource.o
libc += mount.o
libc += eventfd.o
libc += timerfd.o
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

ifneq ($(musl_arch), notsup)
musl += fenv/fegetexceptflag.o
musl += fenv/feholdexcept.o
musl += fenv/fesetexceptflag.o
musl += fenv/$(musl_arch)/fenv.o
endif

musl += crypt/crypt_blowfish.o
musl += crypt/crypt.o
musl += crypt/crypt_des.o
musl += crypt/crypt_md5.o
musl += crypt/crypt_r.o
musl += crypt/crypt_sha256.o
musl += crypt/crypt_sha512.o

#include $(src)/fs/build.mk:

fs :=

fs +=	fs.o \
	unsupported.o

fs +=	vfs/main.o \
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

fs +=	ramfs/ramfs_vfsops.o \
	ramfs/ramfs_vnops.o

fs +=	devfs/devfs_vnops.o \
	devfs/device.o

fs +=	procfs/procfs_vnops.o

objects += $(addprefix fs/, $(fs))
objects += $(addprefix libc/, $(libc))
objects += $(addprefix musl/src/, $(musl))

ifeq ($(cxx_lib_env), host)
    libstdc++.a := $(shell $(CXX) -print-file-name=libstdc++.a)
    ifeq ($(filter /%,$(libstdc++.a)),)
        $(error Error: libstdc++.a needs to be installed.)
    endif

    libsupc++.a := $(shell $(CXX) -print-file-name=libsupc++.a)
    ifeq ($(filter /%,$(libsupc++.a)),)
        $(error Error: libsupc++.a needs to be installed.)
    endif
else
    libstdc++.a := $(shell find $(gccbase)/ -name libstdc++.a)
    libsupc++.a := $(shell find $(gccbase)/ -name libsupc++.a)
endif

ifeq ($(gcc_lib_env), host)
    libgcc.a := $(shell $(CC) -print-libgcc-file-name)
    ifeq ($(filter /%,$(libgcc.a)),)
        $(error Error: libgcc.a needs to be installed.)
    endif

    libgcc_eh.a := $(shell $(CC) -print-file-name=libgcc_eh.a)
    ifeq ($(filter /%,$(libgcc_eh.a)),)
        $(error Error: libgcc_eh.a needs to be installed.)
    endif
else
    libgcc.a := $(shell find $(gccbase)/ -name libgcc.a |  grep -v /32/)
    libgcc_eh.a := $(shell find $(gccbase)/ -name libgcc_eh.a |  grep -v /32/)
endif

ifeq ($(boost_env), host)
    # link with -mt if present, else the base version (and hope it is multithreaded)
    boost-mt := -mt
    boost-lib-dir := $(dir $(shell $(CC) --print-file-name libboost_system$(boost-mt).a))
    ifeq ($(filter /%,$(boost-lib-dir)),)
        boost-mt :=
        boost-lib-dir := $(dir $(shell $(CC) --print-file-name libboost_system$(boost-mt).a))
        ifeq ($(filter /%,$(boost-lib-dir)),)
            $(error Error: libboost_system.a needs to be installed.)
        endif
    endif
    # When boost_env=host, we won't use "-nostdinc", so the build machine's
    # header files will be used normally. So we don't need to add anything
    # special for Boost.
    boost-includes =
else
    boost-lib-dir := $(firstword $(dir $(shell find $(miscbase)/ -name libboost_system*.a)))
    boost-mt := $(if $(filter %-mt.a, $(wildcard $(boost-lib-dir)/*.a)),-mt)
    boost-includes = -isystem $(miscbase)/usr/include
endif

boost-libs := $(boost-lib-dir)/libboost_program_options$(boost-mt).a \
              $(boost-lib-dir)/libboost_system$(boost-mt).a

# ld has a known bug (https://sourceware.org/bugzilla/show_bug.cgi?id=6468)
# where if the executable doesn't use shared libraries, its .dynamic section
# is dropped, even when we use the "--export-dynamic" (which is silently
# ignored). The workaround is to link loader.elf with a do-nothing library.
$(out)/dummy-shlib.so: $(out)/dummy-shlib.o
	$(call quiet, $(CXX) -nodefaultlibs -shared $(gcc-sysroot) -o $@ $^, LINK $@)

$(out)/loader.elf: $(out)/arch/$(arch)/boot.o arch/$(arch)/loader.ld $(out)/loader.o $(out)/runtime.o $(drivers:%=$(out)/%) $(objects:%=$(out)/%) $(out)/bootfs.bin $(out)/dummy-shlib.so
	$(call quiet, $(LD) -o $@ --defsym=OSV_KERNEL_BASE=$(kernel_base) \
		-Bdynamic --export-dynamic --eh-frame-hdr --enable-new-dtags \
	    $(filter-out %.bin, $(^:%.ld=-T %.ld)) \
	    --whole-archive \
	      $(libstdc++.a) $(libgcc.a) $(libgcc_eh.a) \
	      $(boost-libs) \
	    --no-whole-archive, \
		LINK loader.elf)
	@# Build libosv.so matching this loader.elf. This is not a separate
	@# rule because that caused bug #545.
	@readelf --dyn-syms $(out)/loader.elf > $(out)/osv.syms
	@scripts/libosv.py $(out)/osv.syms $(out)/libosv.ld `scripts/osv-version.sh` | $(CC) -c -o $(out)/osv.o -x assembler -
	$(call quiet, $(CC) $(out)/osv.o -nostdlib -shared -o $(out)/libosv.so -T $(out)/libosv.ld, LIBOSV.SO)

$(out)/bsd/%.o: COMMON += -DSMP -D'__FBSDID(__str__)=extern int __bogus__'

$(out)/bootfs.bin: scripts/mkbootfs.py bootfs.manifest.skel $(tools:%=$(out)/%) \
		$(out)/zpool.so $(out)/zfs.so
	$(call quiet, olddir=`pwd`; cd $(out); $$olddir/scripts/mkbootfs.py -o bootfs.bin -d bootfs.bin.d -m $$olddir/bootfs.manifest.skel \
		-D jdkbase=$(jdkbase) -D gccbase=$(gccbase) -D \
		glibcbase=$(glibcbase) -D miscbase=$(miscbase), MKBOOTFS $@)

$(out)/bootfs.o: $(out)/bootfs.bin
$(out)/bootfs.o: ASFLAGS += -I$(out)

$(out)/tools/mkfs/mkfs.so: $(out)/tools/mkfs/mkfs.o $(out)/libzfs.so
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -o $@ $(out)/tools/mkfs/mkfs.o -L$(out) -lzfs, LINK mkfs.so)

$(out)/tools/cpiod/cpiod.so: $(out)/tools/cpiod/cpiod.o $(out)/tools/cpiod/cpio.o $(out)/libzfs.so
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -o $@ $(out)/tools/cpiod/cpiod.o $(out)/tools/cpiod/cpio.o -L$(out) -lzfs, LINK cpiod.so)

$(out)/tools/libtools.so: $(out)/tools/route/route_info.o $(out)/tools/ifconfig/network_interface.o
	$(makedir)
	 $(call quiet, $(CC) $(CFLAGS) -shared -o $(out)/tools/libtools.so $^, LINK libtools.so)


################################################################################
# The dependencies on header files are automatically generated only after the
# first compilation, as explained above. However, header files generated by
# the Makefile are special, in that they need to be created even *before* the
# first compilation. Moreover, some (namely version.h) need to perhaps be
# re-created on every compilation. "generated-headers" is used as an order-
# only dependency on C compilation rules above, so we don't try to compile
# C code before generating these headers.
generated-headers: $(out)/gen/include/bits/alltypes.h perhaps-modify-version-h
.PHONY: generated-headers

# While other generated headers only need to be generated once, version.h
# should be recreated on every compilation. To avoid a cascade of
# recompilation, the rule below makes sure not to modify version.h's timestamp
# if the version hasn't changed.
perhaps-modify-version-h:
	$(call quiet, sh scripts/gen-version-header $(out)/gen/include/osv/version.h, GEN gen/include/osv/version.h)
.PHONY: perhaps-modify-version-h

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

# Note: zfs_prop.c and zprop_common.c are also used by the kernel, thus the manual targets.
$(out)/bsd/cddl/contrib/opensolaris/lib/libzfs/common/zfs_prop.o: bsd/sys/cddl/contrib/opensolaris/common/zfs/zfs_prop.c
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -c -o $@ $<, CC $<)

$(out)/bsd/cddl/contrib/opensolaris/lib/libzfs/common/zprop_common.o: bsd/sys/cddl/contrib/opensolaris/common/zfs/zprop_common.c
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -c -o $@ $<, CC $<)

$(out)/libzfs.so: $(libzfs-objects) $(out)/libuutil.so
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -o $@ $(libzfs-objects) -L$(out) -luutil, LINK libzfs.so)

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
