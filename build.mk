ifndef CROSS_PREFIX
    HOST_CXX=$(CXX)
    STRIP=strip
    OBJCOPY=objcopy
else
    HOST_CXX=g++
    CXX=$(CROSS_PREFIX)g++
    CC=$(CROSS_PREFIX)gcc
    LD=$(CROSS_PREFIX)ld
    STRIP=$(CROSS_PREFIX)strip
    OBJCOPY=$(CROSS_PREFIX)objcopy
endif

build_env ?= external
ifeq ($(build_env), host)
    gcc_lib_env ?= host
    cxx_lib_env ?= host
else
    gcc_lib_env ?= external
    cxx_lib_env ?= external
endif

detect_arch=$(shell echo $(1) | $(CC) -E -xc - | tail -n 1)

ifndef ARCH
    ifeq ($(call detect_arch, __x86_64__),1)
        arch = x64
    endif
    ifeq ($(call detect_arch, __aarch64__),1)
        arch = aarch64
    endif
else
    arch = $(ARCH)
endif
$(info build.mk:)
$(info build.mk: building arch=$(arch), override with ARCH env)
$(info build.mk: building build_env=$(build_env) gcc_lib_env=$(gcc_lib_env) cxx_lib_env=$(cxx_lib_env))
$(info build.mk:)

image ?= default
img_format ?= qcow2
fs_size_mb ?= 10240
local-includes =
INCLUDES = $(local-includes) -I$(src)/arch/$(arch) -I$(src) -I$(src)/include  -I$(src)/arch/common
INCLUDES += -isystem $(src)/include/glibc-compat

glibcbase = $(src)/external/$(arch)/glibc.bin
gccbase = $(src)/external/$(arch)/gcc.bin
miscbase = $(src)/external/$(arch)/misc.bin
jdkbase := $(shell find $(src)/external/$(arch)/openjdk.bin/usr/lib/jvm \
                         -maxdepth 1 -type d -name 'java*')

gcc-inc-base := $(dir $(shell find $(gccbase)/ -name vector | grep -v -e debug/vector$$ -e profile/vector$$))
gcc-inc-base2 := $(dir $(shell find $(gccbase)/ -name unwind.h))
gcc-inc-base3 := $(dir $(shell dirname `find $(gccbase)/ -name c++config.h | grep -v /32/`))

INCLUDES += -isystem $(gcc-inc-base)
INCLUDES += -isystem $(gcc-inc-base3)

ifeq ($(arch),x64)
INCLUDES += -isystem $(src)/external/$(arch)/acpica/source/include
endif

INCLUDES += -isystem $(miscbase)/usr/include
INCLUDES += -isystem $(src)/include/api
INCLUDES += -isystem $(src)/include/api/$(arch)
# must be after include/api, since it includes some libc-style headers:
INCLUDES += -isystem $(gcc-inc-base2)
INCLUDES += -isystem gen/include
INCLUDES += $(post-includes-bsd)

post-includes-bsd += -isystem $(src)/bsd/sys
# For acessing machine/ in cpp xen drivers
post-includes-bsd += -isystem $(src)/bsd/
post-includes-bsd += -isystem $(src)/bsd/$(arch)

ifneq ($(werror),0)
	CFLAGS_WERROR = -Werror
endif
# $(call compiler-flag, -ffoo, option, file)
#     returns option if file builds with -ffoo, empty otherwise
compiler-flag = $(shell $(CXX) $(CFLAGS_WERROR) $1 -o /dev/null -c $3  > /dev/null 2>&1 && echo $2)

compiler-specific := $(call compiler-flag, -std=gnu++11, -DHAVE_ATTR_COLD_LABEL, $(src)/compiler/attr/cold-label.cc)

source-dialects = -D_GNU_SOURCE

bsd/%.o: source-dialects =

# libc has its own source dialect control
libc/%.o: source-dialects =

kernel-defines = -D_KERNEL $(source-dialects)

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
EXTRA_FLAGS = -D__OSV_CORE__
COMMON = $(autodepend) -g -Wall -Wno-pointer-arith $(CFLAGS_WERROR) -Wformat=0 -Wno-format-security \
	-D __BSD_VISIBLE=1 -U _FORTIFY_SOURCE -fno-stack-protector $(INCLUDES) \
	$(kernel-defines) \
	-fno-omit-frame-pointer $(compiler-specific) \
	-include $(src)/compiler/include/intrinsics.hh \
	$(do-sys-includes) \
	$(arch-cflags) $(conf-opt) $(acpi-defines) $(tracing-flags) \
	$(configuration) -nostdinc -D__OSV__ -D__XEN_INTERFACE_VERSION__="0x00030207" $(EXTRA_FLAGS)

tracing-flags-0 =
tracing-flags-1 = -finstrument-functions -finstrument-functions-exclude-file-list=c++,trace.cc,trace.hh,align.hh
tracing-flags = $(tracing-flags-$(conf-tracing))

gcc-opt-Og := $(call compiler-flag, -Og, -Og, $(src)/compiler/empty.cc)

CXXFLAGS = -std=gnu++11 $(COMMON)
CFLAGS = -std=gnu99 $(COMMON)

# should be limited to files under libc/ eventually
CFLAGS += -I $(src)/libc/internal -I  $(src)/libc/arch/$(arch) \
	-Wno-missing-braces -Wno-parentheses -Wno-unused-but-set-variable

ASFLAGS = -g $(autodepend) -DASSEMBLY

fs/vfs/main.o: CXXFLAGS += -Wno-sign-compare -Wno-write-strings

bsd/%.o: INCLUDES += -isystem $(src)/bsd/sys
bsd/%.o: INCLUDES += -isystem $(src)/bsd/
# for machine/
bsd/%.o: INCLUDES += -isystem $(src)/bsd/$(arch)

configuration-defines = conf-preempt conf-debug_memory conf-logger_debug

configuration = $(foreach cf,$(configuration-defines), \
                      -D$(cf:conf-%=CONF_%)=$($(cf)))

include $(src)/conf/base.mak
include $(src)/conf/$(mode).mak

ifeq ($(mode),debug)
CFLAGS += -Wno-maybe-uninitialized
CXXFLAGS += -Wno-maybe-uninitialized
endif

# Add -DNDEBUG if conf-DEBUG_BUILD is set to 0 in *.mak files above
ifeq ($(conf-DEBUG_BUILD),0)
configuration += -DNDEBUG
endif

ifeq ($(arch),x64)
arch-cflags = -msse2
endif

ifeq ($(arch),aarch64)
# You will die horribly without -mstrict-align, due to
# unaligned access to a stack attr variable with stp.
# Relaxing alignment checks via sctlr_el1 A bit setting should solve
# but it doesn't - setting ignored?
#
# Also, mixed TLS models resulted in different var addresses seen by
# different objects depending on the TLS model used.
# Force all __thread variables encountered to local exec.
arch-cflags = -mstrict-align -mtls-dialect=desc -ftls-model=local-exec -DAARCH64_PORT_STUB
endif

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)

makedir = $(call very-quiet, mkdir -p $(dir $@))
build-cxx = $(CXX) $(CXXFLAGS) -c -o $@ $<
q-build-cxx = $(call quiet, $(build-cxx), CXX $@)
build-c = $(CC) $(CFLAGS) -c -o $@ $<
q-build-c = $(call quiet, $(build-c), CC $@)
build-s = $(CXX) $(CXXFLAGS) $(ASFLAGS) -c -o $@ $<
q-build-s = $(call quiet, $(build-s), AS $@)
build-so = $(CC) $(CFLAGS) -o $@ $^
q-build-so = $(call quiet, $(build-so), CC $@)
adjust-deps = sed -i 's! $(subst .,\.,$<)\b! !g' $(@:.o=.d)
q-adjust-deps = $(call very-quiet, $(adjust-deps))

%.o: %.cc
	$(makedir)
	$(q-build-cxx)
	$(q-adjust-deps)

%.o: %.c
	$(makedir)
	$(q-build-c)
	$(q-adjust-deps)

%.o: %.S
	$(makedir)
	$(q-build-s)

%.o: %.s
	$(makedir)
	$(q-build-s)

%.class: %.java
	$(makedir)
	$(call quiet, javac -d $(javabase) -cp $(src)/$(javabase) $^, JAVAC $@)

tests/%.o: COMMON += -fPIC -DBOOST_TEST_DYN_LINK

%.so: EXTRA_FLAGS = -fPIC -shared
%.so: %.o
	$(makedir)
	$(q-build-so)

sys-includes = $(jdkbase)/include $(jdkbase)/include/linux
autodepend = -MD -MT $@ -MP

do-sys-includes = $(foreach inc, $(sys-includes), -isystem $(inc))

ifeq ($(arch),aarch64)
boost-tests :=
else
boost-tests := tests/tst-rename.so
boost-tests += tests/tst-vfs.so
boost-tests += tests/tst-libc-locking.so
boost-tests += tests/misc-fs-stress.so
boost-tests += tests/misc-bdev-write.so
boost-tests += tests/misc-bdev-wlatency.so
boost-tests += tests/misc-bdev-rw.so
boost-tests += tests/tst-promise.so
boost-tests += tests/tst-dlfcn.so
boost-tests += tests/tst-stat.so
boost-tests += tests/tst-wait-for.so
boost-tests += tests/tst-bsd-tcp1.so
boost-tests += tests/tst-async.so
boost-tests += tests/tst-rcu-list.so
boost-tests += tests/tst-tcp-listen.so
endif

ifeq ($(arch),aarch64)
java_tests :=
else
java_tests := tests/hello/Hello.class
endif

ifeq ($(arch),aarch64)
tests :=
else
tests := tests/tst-pthread.so tests/tst-ramdisk.so
tests += tests/tst-vblk.so tests/bench/bench.jar tests/reclaim/reclaim.jar
tests += tests/tst-bsd-evh.so tests/misc-bsd-callout.so
tests += tests/tst-bsd-kthread.so
tests += tests/tst-bsd-taskqueue.so
tests += tests/tst-fpu.so
tests += tests/tst-preempt.so
tests += tests/tst-tracepoint.so
tests += tests/tst-hub.so
tests += tests/misc-leak.so
tests += tests/misc-mmap-anon-perf.so
tests += tests/tst-mmap-file.so
tests += tests/misc-mmap-big-file.so
tests += tests/tst-mmap.so
tests/tst-mmap.so: COMMON += -Wl,-z,now
tests += tests/tst-huge.so
tests += tests/tst-elf-permissions.so
tests/tst-elf-permissions.so: COMMON += -Wl,-z,relro
tests += tests/misc-mutex.so
tests += tests/misc-sockets.so
tests += tests/tst-condvar.so
tests += tests/tst-queue-mpsc.so
tests += tests/tst-af-local.so
tests += tests/tst-pipe.so
tests += tests/tst-yield.so
tests += tests/misc-ctxsw.so
tests += tests/tst-readdir.so
tests += tests/tst-read.so
tests += tests/tst-remove.so
tests += tests/misc-wake.so
tests += tests/tst-epoll.so
tests += tests/misc-lfring.so
tests += tests/tst-fsx.so
tests += tests/tst-sleep.so
tests += tests/tst-resolve.so
tests += tests/tst-except.so
tests += tests/misc-tcp-sendonly.so
tests += tests/tst-tcp-nbwrite.so
tests += tests/misc-tcp-hash-srv.so
tests += tests/misc-loadbalance.so
tests += tests/misc-scheduler.so
tests += tests/tst-dns-resolver.so
tests += tests/tst-fs-link.so
tests += tests/tst-kill.so
tests += tests/tst-truncate.so
tests += $(boost-tests)
tests += tests/misc-panic.so
tests += tests/tst-utimes.so
tests += tests/misc-tcp.so
tests += tests/tst-strerror_r.so
tests += tests/misc-random.so
tests += tests/misc-urandom.so
tests += tests/tst-commands.so
tests += tests/tst-threadcomplete.so
tests += tests/tst-timerfd.so
tests += tests/tst-nway-merger.so
tests += tests/tst-memmove.so
tests += tests/tst-pthread-clock.so
tests += tests/misc-procfs.so
tests += tests/tst-chdir.so
tests += tests/tst-hello.so
tests += tests/tst-concurrent-init.so
tests += tests/tst-ring-spsc-wraparound.so
tests += tests/tst-shm.so
tests += tests/tst-align.so
tests += tests/misc-tcp-close-without-reading.so
tests += tests/tst-sigwait.so
tests += tests/tst-sampler.so
endif

tests/hello/Hello.class: javabase=tests/hello

ifeq ($(arch),aarch64)
java-targets :=
else
java-targets := java-jars java/java.so
endif

java-jars:
	$(call quiet, cd $(src)/java && mvn package -q -DskipTests=true, MVN $@)
.PHONY: java-jars

tools/%.o: COMMON += -fPIC
tools := tools/ifconfig/ifconfig.so
tools += tools/route/lsroute.so
tools += tools/mkfs/mkfs.so
tools += tools/cpiod/cpiod.so

ifeq ($(arch),aarch64)
tools += tests/tst-hello.so
cmdline = tests/tst-hello.so
endif

ifeq ($(arch),x64)

all: loader.img loader.bin usr.img

boot.bin: arch/x64/boot16.ld arch/x64/boot16.o
	$(call quiet, $(LD) -o $@ -T $^, LD $@)

image-size = $(shell stat --printf %s lzloader.elf)

loader-stripped.elf: loader.elf
	$(call very-quiet, cp loader.elf loader-stripped.elf)
	$(call quiet, $(STRIP) loader-stripped.elf, STRIP loader.elf)

loader.img: boot.bin lzloader.elf
	$(call quiet, dd if=boot.bin of=$@ > /dev/null 2>&1, DD $@ boot.bin)
	$(call quiet, dd if=lzloader.elf of=$@ conv=notrunc seek=128 > /dev/null 2>&1, \
		DD $@ lzloader.elf)
	$(call quiet, $(src)/scripts/imgedit.py setsize $@ $(image-size), IMGEDIT $@)
	$(call quiet, $(src)/scripts/imgedit.py setargs $@ $(cmdline), IMGEDIT $@)

loader.bin: arch/x64/boot32.o arch/x64/loader32.ld
	$(call quiet, $(LD) -nostartfiles -static -nodefaultlibs -o $@ \
	                $(filter-out %.bin, $(^:%.ld=-T %.ld)), LD $@)

arch/x64/boot32.o: loader.elf

fastlz/fastlz.o:
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) -O2 -m32 -o $@ -c $(src)/fastlz/fastlz.cc, CXX $@)

fastlz/lz: fastlz/fastlz.cc fastlz/lz.cc
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLASG) -O2 -o $@ $(filter %.cc, $^), CXX $@)

loader-stripped.elf.lz.o: loader-stripped.elf fastlz/lz
	$(call quiet, $(out)/fastlz/lz $(out)/loader-stripped.elf, LZ $@)
	$(call quiet, objcopy -B i386 -I binary -O elf32-i386 loader-stripped.elf.lz $@, OBJCOPY $@)

fastlz/lzloader.o: fastlz/lzloader.cc
	$(call quiet, $(CXX) $(CXXFLAGS) -O2 -m32 -o $@ -c $(src)/fastlz/lzloader.cc, CXX $@)

lzloader.elf: loader-stripped.elf.lz.o fastlz/lzloader.o arch/x64/lzloader.ld \
	fastlz/fastlz.o
	$(call quiet, $(src)/scripts/check-image-size.sh loader-stripped.elf 23068672)
	$(call quiet, $(LD) -o $@ \
		-Bdynamic --export-dynamic --eh-frame-hdr --enable-new-dtags \
	-T $(src)/arch/x64/lzloader.ld \
	$(patsubst %.o,$(out)/%.o, $(filter %.o, $^)), LD $@)

acpi-defines = -DACPI_MACHINE_WIDTH=64 -DACPI_USE_LOCAL_CACHE

acpi-source := $(shell find $(src)/external/$(arch)/acpica/source/components -type f -name '*.c')
acpi = $(patsubst $(src)/%.c, %.o, $(acpi-source))

$(acpi): CFLAGS += -fno-strict-aliasing -Wno-strict-aliasing

endif # x64

ifeq ($(arch),aarch64)

all: loader.img

preboot.elf: arch/$(arch)/preboot.ld arch/$(arch)/preboot.o
	$(call quiet, $(LD) -o $@ -T $^, LD $@)

preboot.bin: preboot.elf
	$(call quiet, $(OBJCOPY) -O binary $^ $@, OBJCOPY $@)

image-size = $(shell stat --printf %s loader-stripped.elf)

loader-stripped.elf: loader.elf
	$(call very-quiet, cp loader.elf loader-stripped.elf)
	$(call quiet, $(STRIP) loader-stripped.elf, STRIP loader.elf)

loader.img: preboot.bin loader-stripped.elf
	$(call quiet, dd if=preboot.bin of=$@ > /dev/null 2>&1, DD $@ preboot.bin)
	$(call quiet, dd if=loader-stripped.elf of=$@ conv=notrunc obs=4096 seek=16 > /dev/null 2>&1, DD $@ loader-stripped.elf)
	$(call quiet, $(src)/scripts/imgedit.py setsize $@ $(image-size), IMGEDIT $@)
	$(call quiet, $(src)/scripts/imgedit.py setargs $@ $(cmdline), IMGEDIT $@)

endif # aarch64

bsd/sys/crypto/sha2/sha2.o: CFLAGS+=-Wno-strict-aliasing
bsd/sys/crypto/rijndael/rijndael-api-fst.o: CFLAGS+=-Wno-strict-aliasing

include $(src)/bsd/cddl/contrib/opensolaris/lib/libuutil/common/build.mk
include $(src)/bsd/cddl/contrib/opensolaris/lib/libzfs/common/build.mk
include $(src)/bsd/cddl/contrib/opensolaris/cmd/zpool/build.mk
include $(src)/bsd/cddl/contrib/opensolaris/cmd/zfs/build.mk

bsd  = bsd/net.o  
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
bsd += bsd/sys/netinet/tcp_offload.o
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
bsd += bsd/sys/xdr/xdr.o
bsd += bsd/sys/xdr/xdr_array.o
bsd += bsd/sys/xdr/xdr_mem.o

ifeq ($(arch),x64)
bsd/%.o: COMMON += -DXEN -DXENHVM
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

bsd/sys/%.o: COMMON += -Wno-sign-compare -Wno-narrowing -Wno-write-strings -Wno-parentheses -Wno-unused-but-set-variable

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

solaris-tests += tests/tst-solaris-taskq.so

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

zfs-tests += tests/misc-zfs-disk.so
zfs-tests += tests/misc-zfs-io.so
zfs-tests += tests/misc-zfs-arc.so

tests += tests/tst-zfs-mount.so

solaris += $(zfs)
solaris-tests += $(zfs-tests)

$(zfs) $(zfs-tests): CFLAGS+= \
	-DBUILDING_ZFS \
	-I$(src)/bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs \
	-I$(src)/bsd/sys/cddl/contrib/opensolaris/common/zfs

$(solaris) $(solaris-tests): CFLAGS+= \
	-Wno-strict-aliasing \
	-Wno-unknown-pragmas \
	-Wno-unused-variable \
	-Wno-switch \
	-Wno-maybe-uninitialized \
	-I$(src)/bsd/sys/cddl/compat/opensolaris \
	-I$(src)/bsd/sys/cddl/contrib/opensolaris/common \
	-I$(src)/bsd/sys/cddl/contrib/opensolaris/uts/common \
	-I$(src)/bsd/sys

$(solaris): ASFLAGS+= \
	-I$(src)/bsd/sys/cddl/contrib/opensolaris/uts/common

tests += $(solaris-tests)

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
drivers += drivers/clockevent.o
drivers += drivers/ramdisk.o
drivers += core/elf.o
drivers += java/jvm_balloon.o
drivers += java/java_api.o
drivers += drivers/random.o
drivers += drivers/zfs.o

ifeq ($(arch),x64)
drivers += $(libtsm)
drivers += drivers/vga.o drivers/kbd.o drivers/isa-serial.o
drivers += core/interrupt.o
drivers += core/pvclock-abi.o
drivers += drivers/device.o
drivers += drivers/pci-device.o drivers/pci-function.o drivers/pci-bridge.o
drivers += drivers/driver.o
drivers += drivers/virtio.o
drivers += drivers/virtio-vring.o
drivers += drivers/virtio-net.o
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
drivers += drivers/pci.o
drivers += drivers/scsi-common.o
drivers += drivers/vmw-pvscsi.o
endif # x64

ifeq ($(arch),aarch64)
drivers += drivers/pl011.o
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

ifeq ($(arch),aarch64)
objects += arch/$(arch)/arm-clock.o
objects += arch/$(arch)/gic.o
endif

ifeq ($(arch),x64)
objects += arch/x64/arch-trace.o
objects += arch/x64/ioapic.o
objects += arch/x64/math.o
objects += arch/x64/apic.o
objects += arch/x64/apic-clock.o
objects += arch/x64/cpuid.o
objects += arch/x64/entry-xen.o
objects += arch/x64/xen.o
objects += arch/x64/xen_intr.o
objects += core/sampler.o
objects += $(acpi)
endif # x64

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
objects += core/callstack.o
objects += core/poll.o
objects += core/select.o
objects += core/epoll.o
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

include $(src)/fs/build.mk
include $(src)/libc/build.mk

objects += $(addprefix fs/, $(fs))
objects += $(addprefix libc/, $(libc))

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
    libgcc_s.a := $(shell find $(gccbase)/ -name libgcc.a |  grep -v /32/)
    libgcc_eh.a := $(shell find $(gccbase)/ -name libgcc_eh.a |  grep -v /32/)
endif

boost-lib-dir := $(shell dirname `find $(miscbase)/ -name libboost_system-mt.a`)

boost-libs := $(boost-lib-dir)/libboost_program_options-mt.a \
              $(boost-lib-dir)/libboost_system-mt.a

$(boost-tests): $(boost-lib-dir)/libboost_unit_test_framework-mt.so \
                $(boost-lib-dir)/libboost_filesystem-mt.so

dummy-shlib.so: dummy-shlib.o
	$(call quiet, $(CXX) -nodefaultlibs -shared -o $@ $^, LD $@)

loader.elf: arch/$(arch)/boot.o arch/$(arch)/loader.ld loader.o runtime.o $(drivers) \
	$(objects) dummy-shlib.so bootfs.bin
	$(call quiet, $(LD) -o $@ \
		-Bdynamic --export-dynamic --eh-frame-hdr --enable-new-dtags \
	    $(filter-out %.bin, $(^:%.ld=-T %.ld)) \
	    --whole-archive \
	      $(libstdc++.a) $(libgcc_s.a) $(libgcc_eh.a) \
	      $(boost-libs) \
	    --no-whole-archive, \
		LD $@)

bsd/%.o: COMMON += -DSMP -D'__FBSDID(__str__)=extern int __bogus__'

jni = java/jni/balloon.so java/jni/elf-loader.so java/jni/networking.so \
	java/jni/stty.so java/jni/tracepoint.so java/jni/power.so java/jni/monitor.so

loader-size = $(shell stat --printf %s loader.img)
zfs-start = $(shell echo $$(($(loader-size)+2097151 & ~2097151)))
zfs-size = $(shell echo $$(($(fs_size_mb) * 1024 * 1024 - $(zfs-start))))

bare.raw: loader.img
	$(call quiet, qemu-img create $@ 100M, QEMU-IMG CREATE $@)
	$(call quiet, dd if=loader.img of=$@ conv=notrunc > /dev/null 2>&1)
	$(call quiet, $(src)/scripts/imgedit.py setpartition $@ 2 $(zfs-start) $(zfs-size), IMGEDIT $@)

bare.img: scripts/mkzfs.py $(jni) bare.raw $(out)/bootfs.manifest
	$(call quiet, echo Creating $@ as $(img_format))
	$(call quiet, qemu-img convert -f raw -O $(img_format) bare.raw $@)
	$(call quiet, qemu-img resize $@ +$(fs_size_mb)M > /dev/null 2>&1)
	$(src)/scripts/mkzfs.py -o $@ -d $@.d -m $(out)/bootfs.manifest

usr.img: bare.img $(out)/usr.manifest $(out)/cmdline
	$(call quiet, cp bare.img $@)
	$(src)/scripts/upload_manifest.py -o $@ -m $(out)/usr.manifest \
		-D jdkbase=$(jdkbase) -D gccbase=$(gccbase) -D \
		glibcbase=$(glibcbase) -D miscbase=$(miscbase)
	$(call quiet, $(src)/scripts/imgedit.py setargs $@ "$(shell cat $(out)/cmdline)", IMGEDIT $@)

osv.vmdk osv.vdi:
	$(call quiet, echo Creating $@ as $(subst osv.,,$@))
	$(call quiet, qemu-img convert -O $(subst osv.,,$@) usr.img $@)
.PHONY: osv.vmdk osv.vdi

$(jni): INCLUDES += -I /usr/lib/jvm/java/include -I /usr/lib/jvm/java/include/linux/

bootfs.bin: scripts/mkbootfs.py $(java-targets) $(out)/bootfs.manifest $(tests) $(java_tests) $(tools) \
		tests/testrunner.so \
		zpool.so zfs.so
	$(call quiet, $(src)/scripts/mkbootfs.py -o $@ -d $@.d -m $(out)/bootfs.manifest \
		-D jdkbase=$(jdkbase) -D gccbase=$(gccbase) -D \
		glibcbase=$(glibcbase) -D miscbase=$(miscbase), MKBOOTFS $@)

bootfs.o: bootfs.bin

tools/mkfs/mkfs.so: tools/mkfs/mkfs.o libzfs.so

tools/cpiod/cpiod.so: tools/cpiod/cpiod.o tools/cpiod/cpio.o libzfs.so

runtime.o: gen/include/ctype-data.h

gen/include/ctype-data.h: gen-ctype-data
	$(call quiet, ./gen-ctype-data > $@, GEN $@)

gen-ctype-data: gen-ctype-data.cc
	$(call quiet, $(HOST_CXX) -o $@ $^, HOST_CXX $@)

generated-headers = gen/include/bits/alltypes.h
generated-headers += gen/include/osv/version.h

gen/include/bits/alltypes.h: $(src)/include/api/$(arch)/bits/alltypes.h.sh
	$(call very-quiet, mkdir -p $(dir $@))
	$(call quiet, sh $^ > $@, GEN $@)

gen/include/osv/version.h: $(src)/scripts/gen-version-header
	$(call quiet, sh $(src)/scripts/gen-version-header $@, GEN $@)
.PHONY: gen/include/osv/version.h

$(src)/build.mk: $(generated-headers)

# Automatically generate modules/tests/usr.manifest which includes all tests.
$(src)/modules/tests/usr.manifest: $(src)/build.mk
	@echo "  generating modules/tests/usr.manifest"
	@cat $@.skel > $@
	@echo $(tests) | tr ' ' '\n' | awk '{print "/" $$0 ": ./" $$0}' >> $@
	@echo $(java_tests) | tr ' ' '\n' | \
	    awk '{a=$$0; sub(".*/","",a); print "/java/" a ": ./" $$0}' >> $@

################################################################################

.PHONY: process-modules
process-modules: bootfs.manifest.skel usr.manifest.skel $(src)/modules/tests/usr.manifest $(java-targets)
	cd $(out)/module \
	  && jdkbase=$(jdkbase) OSV_BASE=$(src) OSV_BUILD_PATH=$(out) MAKEFLAGS= $(src)/scripts/module.py --image-config $(image)

$(out)/cmdline: process-modules
$(out)/bootfs.manifest: process-modules
$(out)/usr.manifest: process-modules

-include $(shell find -name '*.d')

.DELETE_ON_ERROR:
