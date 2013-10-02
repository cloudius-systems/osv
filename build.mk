
arch = x64
# cmdline = java.so -jar /java/cli.jar
# cmdline = java.so -jar /java/web.jar -cp java/cli.jar app
cmdline = java.so -jar /usr/mgmt/web-1.0.0.jar app prod
#cmdline = testrunner.so
#cmdline = java.so Hello
local-includes =
INCLUDES = $(local-includes) -I. -I$(src)/arch/$(arch) -I$(src) -I$(src)/external/libunwind/include -I$(src)/include
INCLUDES += -isystem $(src)/include/glibc-compat
gcc-inc-base = $(src)/external/gcc.bin/usr/include/c++/4.8.1
gcc-inc-base2 = $(src)/external/gcc.bin/usr/lib/gcc/x86_64-redhat-linux/4.8.1/include
INCLUDES += -isystem $(gcc-inc-base)
INCLUDES += -isystem $(gcc-inc-base)/x86_64-redhat-linux
INCLUDES += -isystem $(src)/external/acpica/source/include
INCLUDES += -isystem $(src)/external/misc.bin/usr/include
INCLUDES += -isystem $(src)/include/api
INCLUDES += -isystem $(src)/include/api/x86_64
# must be after include/api, since it includes some libc-style headers:
INCLUDES += -isystem $(gcc-inc-base2)
INCLUDES += -isystem gen/include
INCLUDES += $(post-includes-bsd)

post-includes-bsd += -isystem $(src)/bsd/sys
# For acessing machine/ in cpp xen drivers
post-includes-bsd += -isystem $(src)/bsd/

# $(call compiler-flag, -ffoo, option, file)
#     returns option if file builds with -ffoo, empty otherwise
compiler-flag = $(shell $(CXX) -Werror $1 -o /dev/null -c $3  > /dev/null 2>&1 && echo $2)

compiler-specific := $(call compiler-flag, -std=gnu++11, -DHAVE_ATTR_COLD_LABEL, $(src)/compiler/attr/cold-label.cc)

kernel-defines = -D_KERNEL

COMMON = $(autodepend) -g -Wall -Wno-pointer-arith -Werror -Wformat=0 \
	-D __BSD_VISIBLE=1 -U _FORTIFY_SOURCE -fno-stack-protector $(INCLUDES) \
	$(kernel-defines) \
	-fno-omit-frame-pointer $(compiler-specific) \
	-include $(src)/compiler/include/intrinsics.hh \
	$(do-sys-includes) \
	$(arch-cflags) $(conf-opt) $(acpi-defines) $(tracing-flags) \
	$(configuration) -nostdinc -D__OSV__ -D__XEN_INTERFACE_VERSION__="0x00030207"

tracing-flags-0 =
tracing-flags-1 = -finstrument-functions -finstrument-functions-exclude-file-list=c++,trace.cc,trace.hh,align.hh
tracing-flags = $(tracing-flags-$(conf-tracing))

CXXFLAGS = -std=gnu++11 $(COMMON)
CFLAGS = -std=gnu99 $(COMMON)

# should be limited to files under libc/ eventually
CFLAGS += -I $(src)/libc/internal -I  $(src)/libc/arch/$(arch) \
	-Wno-missing-braces -Wno-parentheses -Wno-unused-but-set-variable

ASFLAGS = -g $(autodepend) -DASSEMBLY

fs/vfs/main.o: CXXFLAGS += -Wno-sign-compare -Wno-write-strings

bsd/%.o: INCLUDES += -isystem $(src)/bsd/sys
# for machine/
bsd/%.o: INCLUDES += -isystem $(src)/bsd/ 

configuration-defines = conf-preempt conf-debug_memory conf-logger_debug

configuration = $(foreach cf,$(configuration-defines), \
                      -D$(cf:conf-%=CONF_%)=$($(cf)))

include $(src)/conf/base.mak
include $(src)/conf/$(mode).mak

arch-cflags = -msse4.1


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
	
%.o: %.cc
	$(makedir)
	$(q-build-cxx)

%.o: %.c
	$(makedir)
	$(q-build-c)

%.o: %.S
	$(makedir)
	$(q-build-s)

%.o: %.s
	$(makedir)
	$(q-build-s)

%.class: %.java
	$(makedir)
	$(call quiet, javac -d $(javabase) -cp $(src)/$(javabase) $^, JAVAC $@)

tests/%.o: COMMON += -fPIC

%.so: COMMON += -fPIC -shared
%.so: %.o
	$(makedir)
	$(q-build-so)

# Some .so's need to refer to libstdc++ so it will be linked at run time.
# The majority of our .so don't actually need libstdc++, so didn't add it
# by default.
tests/tst-queue-mpsc.so: CFLAGS+=-lstdc++
tests/tst-mutex.so: CFLAGS+=-lstdc++

sys-includes = $(jdkbase)/include $(jdkbase)/include/linux
autodepend = -MD -MT $@ -MP

do-sys-includes = $(foreach inc, $(sys-includes), -isystem $(inc))

tests := tests/tst-pthread.so tests/tst-ramdisk.so tests/hello/Hello.class
tests += tests/tst-vblk.so tests/bench/bench.jar
tests += tests/tst-bsd-evh.so tests/tst-bsd-callout.so
tests += tests/tst-bsd-kthread.so
tests += tests/tst-bsd-taskqueue.so
tests += tests/tst-fpu.so
tests += tests/tst-preempt.so
tests += tests/tst-tracepoint.so
tests += tests/tst-hub.so
tests += tests/tst-leak.so tests/tst-mmap.so tests/tst-vfs.so
tests += tests/tst-mmap-file.so
tests += tests/tst-mutex.so
tests += tests/tst-sockets.so
tests += tests/tst-bsd-tcp1.so
tests += tests/tst-condvar.so
tests += tests/tst-queue-mpsc.so
tests += tests/tst-af-local.so
tests += tests/tst-pipe.so
tests += tests/tst-yield.so
tests += tests/tst-ctxsw.so
tests += tests/tst-readdir.so
tests += tests/tst-wake.so
tests += tests/tst-epoll.so
tests += tests/tst-lfring.so
tests += tests/tst-fsx.so
tests += tests/tst-sleep.so
tests += tests/tst-resolve.so
tests += tests/tst-except.so
tests += tests/tst-tcp-sendonly.so
tests += tests/tst-tcp-hash-srv.so
tests += tests/tst-loadbalance.so
tests += tests/tst-dns-resolver.so
tests += tests/tst-fs-link.so
tests += tests/tst-kill.so

tests/hello/Hello.class: javabase=tests/hello

java/io/osv/RunJava.class: javabase=java
java/runjava.jar: java/io/osv/RunJava.class
	jar cf $@ -C java $(patsubst java/%, %, $^)
	jar i $@

tools/%.o: COMMON += -fPIC
tools := tools/ifconfig/ifconfig.so
tools += tools/route/lsroute.so

all: loader.img loader.bin usr.img

boot.bin: arch/x64/boot16.ld arch/x64/boot16.o
	$(call quiet, $(LD) -o $@ -T $^, LD $@)

image-size = $(shell stat --printf %s loader-stripped.elf)

loader-stripped.elf: loader.elf
	$(call very-quiet, cp loader.elf loader-stripped.elf)
	$(call quiet, strip loader-stripped.elf, STRIP loader.elf)

loader.img: boot.bin loader-stripped.elf
	$(call quiet, dd if=boot.bin of=$@ > /dev/null 2>&1, DD $@ boot.bin)
	$(call quiet, dd if=loader-stripped.elf of=$@ conv=notrunc seek=128 > /dev/null 2>&1, \
		DD $@ loader.elf)
	$(call quiet, $(src)/scripts/imgedit.py setsize $@ $(image-size), IMGEDIT $@)
	$(call quiet, $(src)/scripts/imgedit.py setargs $@ $(cmdline), IMGEDIT $@)

loader-size = $(shell stat --printf %s loader.img)
zfs-start = $(shell echo $$(($(loader-size)+2097151 & ~2097151)))
zfs-size = $(shell echo $$((10737418240 - $(zfs-start))))

loader.bin: arch/x64/boot32.o arch/x64/loader32.ld
	$(call quiet, $(LD) -nostartfiles -static -nodefaultlibs -o $@ \
	                $(filter-out %.bin, $(^:%.ld=-T %.ld)), LD $@)

arch/x64/boot32.o: loader.elf

bsd/sys/crypto/sha2/sha2.o: CFLAGS+=-Wno-strict-aliasing

include $(src)/bsd/cddl/contrib/opensolaris/lib/libuutil/common/build.mk
include $(src)/bsd/cddl/contrib/opensolaris/lib/libzfs/common/build.mk
include $(src)/bsd/cddl/contrib/opensolaris/cmd/zpool/build.mk

bsd  = bsd/net.o  
bsd += bsd/machine/in_cksum.o
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

bsd/%.o: COMMON += -DXEN -DXENHVM
bsd += bsd/sys/xen/gnttab.o
bsd += bsd/sys/xen/evtchn.o
bsd += bsd/sys/xen/xenstore/xenstore.o
bsd += bsd/sys/xen/xenbus/xenbus.o
bsd += bsd/sys/xen/xenbus/xenbusb.o
bsd += bsd/sys/xen/xenbus/xenbusb_front.o
bsd += bsd/sys/dev/xen/netfront/netfront.o
bsd += bsd/sys/dev/xen/blkfront/blkfront.o

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

zfs-tests += tests/tst-zfs-simple.so
zfs-tests += tests/tst-zfs-disk.so

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

tests += $(solaris-tests)

drivers :=
drivers += drivers/console.o drivers/vga.o drivers/isa-serial.o
drivers += drivers/debug-console.o
drivers += drivers/ramdisk.o
drivers += $(bsd) $(solaris)
drivers += core/mmu.o
drivers += core/elf.o
drivers += core/interrupt.o
drivers += core/pvclock-abi.o
drivers += drivers/device.o
drivers += drivers/pci-device.o drivers/pci-function.o drivers/pci-bridge.o 
drivers += drivers/driver.o
drivers += drivers/virtio.o
drivers += drivers/virtio-vring.o
drivers += drivers/virtio-net.o
drivers += drivers/virtio-blk.o
drivers += drivers/clock.o drivers/kvmclock.o drivers/xenclock.o
drivers += drivers/clockevent.o
drivers += drivers/acpi.o
drivers += drivers/hpet.o
drivers += drivers/xenfront.o drivers/xenfront-xenbus.o drivers/xenfront-blk.o

objects = bootfs.o
objects += arch/x64/exceptions.o
objects += arch/x64/entry.o
objects += arch/x64/ioapic.o
objects += arch/x64/math.o
objects += arch/x64/apic.o
objects += arch/x64/apic-clock.o
objects += arch/x64/arch-setup.o
objects += arch/x64/smp.o
objects += arch/x64/signal.o
objects += arch/x64/cpuid.o
objects += arch/x64/string.o
objects += arch/x64/arch-cpu.o
objects += arch/x64/entry-xen.o
objects += arch/x64/xen.o
objects += arch/x64/backtrace.o
objects += arch/x64/xen_intr.o
objects += core/mutex.o
objects += core/lfmutex.o
objects += core/rwlock.o
objects += core/semaphore.o
objects += core/condvar.o
objects += core/eventlist.o
objects += core/debug.o
objects += core/rcu.o
objects += drivers/pci.o
objects += core/mempool.o
objects += core/alloctracker.o
objects += core/printf.o
objects += arch/x64/elf-dl.o
objects += linux.o
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

include $(src)/fs/build.mk
include $(src)/libc/build.mk

objects += $(addprefix fs/, $(fs))
objects += $(addprefix libc/, $(libc))
objects += $(acpi)

acpi-defines = -DACPI_MACHINE_WIDTH=64 -DACPI_USE_LOCAL_CACHE

acpi-source := $(shell find $(src)/external/acpica/source/components -type f -name '*.c')
acpi = $(patsubst $(src)/%.c, %.o, $(acpi-source))

$(acpi): CFLAGS += -fno-strict-aliasing -Wno-strict-aliasing

libstdc++.a = $(shell find $(gccbase) -name libstdc++.a)
libsupc++.a = $(shell find $(gccbase) -name libsupc++.a)
libgcc_s.a = $(shell find $(gccbase) -name libgcc.a |  grep -v /32/)
libgcc_eh.a = $(shell find $(gccbase) -name libgcc_eh.a |  grep -v /32/)

loader.elf: arch/x64/boot.o arch/x64/loader.ld loader.o runtime.o $(drivers) \
        $(objects) dummy-shlib.so \
		bootfs.bin
	$(call quiet, $(LD) -o $@ \
		-Bdynamic --export-dynamic --eh-frame-hdr --enable-new-dtags \
	    $(filter-out %.bin, $(^:%.ld=-T %.ld)) \
	    $(boost-libs) \
	    --whole-archive \
	      $(libstdc++.a) $(libgcc_s.a) $(libgcc_eh.a) \
	    --no-whole-archive \
	    $(src)/libunwind.a, \
		LD $@)

dummy-shlib.so: dummy-shlib.o
	$(call quiet, $(CXX) -nodefaultlibs -shared -o $@ $^, LD $@)

jdkbase := $(shell find $(src)/external/openjdk.bin/usr/lib/jvm \
                         -maxdepth 1 -type d -name 'java*')
glibcbase = $(src)/external/glibc.bin
gccbase = $(src)/external/gcc.bin
miscbase = $(src)/external/misc.bin
boost-lib-dir = $(miscbase)/usr/lib64
boost-libs := $(boost-lib-dir)/libboost_program_options-mt.a \
              $(boost-lib-dir)/libboost_system-mt.a

bsd/%.o: COMMON += -DSMP -D'__FBSDID(__str__)=extern int __bogus__' -D__x86_64__

jni = java/jni/balloon.so java/jni/elf-loader.so java/jni/networking.so \
	java/jni/stty.so java/jni/tracepoint.so java/jni/power.so

usr.img: loader.img scripts/mkzfs.py usr.manifest $(jni)
	$(src)/scripts/mkzfs.py -o $@ -d $@.d -m $(src)/usr.manifest \
		-D jdkbase=$(jdkbase) -D gccbase=$(gccbase) -D \
		glibcbase=$(glibcbase) -D miscbase=$(miscbase) -s $(zfs-start)
	$(call quiet, dd if=loader.img of=$@ conv=notrunc > /dev/null 2>&1)
	$(call quiet, $(src)/scripts/imgedit.py setpartition $@ 2 $(zfs-start) $(zfs-size), IMGEDIT $@)

$(jni): INCLUDES += -I /usr/lib/jvm/java/include -I /usr/lib/jvm/java/include/linux/

bootfs.bin: scripts/mkbootfs.py bootfs.manifest $(tests) $(tools) \
		tests/testrunner.so java/java.so java/runjava.jar \
		zpool.so
	$(call quiet, $(src)/scripts/mkbootfs.py -o $@ -d $@.d -m $(src)/bootfs.manifest \
		-D jdkbase=$(jdkbase) -D gccbase=$(gccbase) -D \
		glibcbase=$(glibcbase) -D miscbase=$(miscbase), MKBOOTFS $@)

bootfs.o: bootfs.bin

runtime.o: ctype-data.h

ctype-data.h: gen-ctype-data
	$(call quiet, ./gen-ctype-data > $@, GEN $@)

gen-ctype-data: gen-ctype-data.o
	$(call quiet, $(CXX) -o $@ $^, LD $@)

generated-headers = gen/include/bits/alltypes.h

gen/include/bits/alltypes.h: $(src)/include/api/x86_64/bits/alltypes.h.sh
	$(call very-quiet, mkdir -p $(dir $@))
	$(call quiet, sh $^ > $@, GEN $@)

$(src)/build.mk: $(generated-headers)

-include $(shell find -name '*.d')

.DELETE_ON_ERROR:
