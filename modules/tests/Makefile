module: usr.manifest common.manifest fs.manifest build_all_tests

include ../common.gmk

# Ask make to not delete "intermediate" results, such as the .o in the chain
# .cc -> .o -> .so. Otherwise, during the first build, make considers the .o
# to be intermediate, and deletes it, but the newly-created ".d" files lists
# the ".o" as a target - so it needs to be created again on the second make.
# See commit fac05c95 for a longer explanation.
.SECONDARY:

COMMON += -g -O2 -fPIC -DBOOST_TEST_DYN_LINK \
	-U _FORTIFY_SOURCE -D_KERNEL -D__OSV__ -DCONF_debug_memory=0 \
	-Wall -Wno-pointer-arith -Wformat=0 -Wno-format-security

LIBS = $(libgcc_s_dir)/libgcc_s.so.1

CXXFLAGS = -std=gnu++11 $(COMMON)
CFLAGS = -std=gnu99 $(COMMON)

# The following are rules for making a .so from .c or .cc. We could have
# done the compilation from .c to .so directly, in one step without an
# intermediate file (which needs to stay behind, as explained above).
# but this prevents "ccache" from working - because it doesn't cache results
# of linking. So with the intermediate step, the (slow) compilation result is
# cached, and just the (fast) linking is redone. This intermediate step
# speeds up "make clean; scripts/build image=tests" from 1:40 minutes to
# just 32 seconds.
$(out)/%.o: $(src)/%.cc
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) -c -o $@ $<, CXX $*.cc)
$(out)/%.o: $(src)/%.c
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -c -o $@ $< , CC $*.c)
$(out)/tests/rofs/%.o: $(src)/tests/%.cc
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) -DREAD_ONLY_FS -c -o $@ $<, CXX $*.cc)
$(out)/tests/rofs/%.o: $(src)/tests/%.c
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -DREAD_ONLY_FS -c -o $@ $< , CC $*.c)
$(out)/%.so: $(out)/%.o
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -o $@ $< $(LIBS), LD $*.so)

$(out)/tests/tst-non-fpic.o: CXXFLAGS:=$(subst -fPIC,-fno-pic -mcmodel=large,$(CXXFLAGS))

$(out)/tests/tst-getopt-pie.o: CXXFLAGS:=$(subst -fPIC,-fpie,$(CXXFLAGS))
$(out)/tests/tst-getopt-pie.o: $(src)/tests/tst-getopt.cc
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) -c -o $@ $<, CXX tests/tst-getopt-pie.cc)
$(out)/tests/tst-getopt-pie.so: $(out)/tests/tst-getopt-pie.o
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -fpie -o $@ $< $(LIBS), LD tests/tst-getopt-pie.so)

$(out)/tests/tst-non-pie.so: CXXFLAGS:=$(subst -fPIC,-no-pie,$(CXXFLAGS))
$(out)/tests/tst-non-pie.so: $(src)/tests/tst-non-pie.cc
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(LIBS), LD tests/tst-non-pie.so)

# The rofs test image mounts /tmp as ramfs and 4 tests that exercise file system
# fail due to some unresolved bugs or other shortcomings of the ramfs implementation
# and are temporarily removed from the rofs-only-tests list. The tests tst-readdir.so
# and tst-rename.so would fail until the issue #68 is fixed. Similarly
# tst-fs-link.so do not pass due to lack of hard links support in the ramfs
# implementation. Lastly tst-fallocate.so also does not work with ramfs due to
# the shortcomings in ramfs implementation of statfs that does not report free/used
# blocks adequately.
#
# These 4 tests are compiled from the same source files but passed in READ_ONLY_FS
# to switch relevant logic in those tests to exercise scenarios applicable
# to read-only filesystem
rofs-only-tests := rofs/tst-chdir.so rofs/tst-symlink.so rofs/tst-readdir.so \
	rofs/tst-concurrent-read.so

zfs-only-tests := tst-readdir.so tst-fallocate.so tst-fs-link.so \
	tst-concurrent-read.so

specific-fs-tests := $($(fs_type)-only-tests)

tests := tst-pthread.so misc-ramdisk.so tst-vblk.so tst-bsd-evh.so \
	misc-bsd-callout.so tst-bsd-kthread.so tst-bsd-taskqueue.so \
	tst-fpu.so tst-preempt.so tst-tracepoint.so tst-hub.so \
	misc-console.so misc-leak.so misc-readbench.so misc-mmap-anon-perf.so \
	tst-mmap-file.so misc-mmap-big-file.so tst-mmap.so tst-huge.so \
	tst-elf-permissions.so misc-mutex.so misc-sockets.so tst-condvar.so \
	tst-queue-mpsc.so tst-af-local.so tst-pipe.so tst-yield.so \
	misc-ctxsw.so tst-read.so tst-symlink.so tst-openat.so \
	tst-eventfd.so tst-remove.so misc-wake.so tst-epoll.so misc-lfring.so \
	misc-fsx.so tst-sleep.so tst-resolve.so tst-except.so \
	misc-tcp-sendonly.so tst-tcp-nbwrite.so misc-tcp-hash-srv.so \
	misc-loadbalance.so misc-scheduler.so tst-console.so tst-app.so \
	misc-setpriority.so misc-timeslice.so misc-tls.so misc-gtod.so \
	tst-dns-resolver.so tst-kill.so tst-truncate.so \
	misc-panic.so tst-utimes.so tst-utimensat.so tst-futimesat.so \
	misc-tcp.so tst-strerror_r.so misc-random.so misc-urandom.so \
	tst-commands.so tst-options.so tst-threadcomplete.so tst-timerfd.so \
	tst-nway-merger.so tst-memmove.so tst-pthread-clock.so misc-procfs.so \
	tst-chdir.so tst-chmod.so tst-hello.so misc-concurrent-io.so \
	tst-concurrent-init.so tst-ring-spsc-wraparound.so tst-shm.so \
	tst-align.so tst-cxxlocale.so misc-tcp-close-without-reading.so \
	tst-sigwait.so tst-sampler.so misc-malloc.so misc-memcpy.so \
	misc-free-perf.so misc-printf.so tst-hostname.so \
	tst-sendfile.so misc-lock-perf.so tst-uio.so tst-printf.so \
	tst-pthread-affinity.so tst-pthread-tsd.so tst-thread-local.so \
	tst-zfs-mount.so tst-regex.so tst-tcp-siocoutq.so \
	tst-select-timeout.so tst-faccessat.so \
	tst-fstatat.so misc-reboot.so tst-fcntl.so payload-namespace.so \
	tst-namespace.so tst-without-namespace.so payload-env.so \
	payload-merge-env.so misc-execve.so misc-execve-payload.so misc-mutex2.so \
	tst-pthread-setcancelstate.so tst-pin.so tst-run.so \
	tst-pthread-affinity-inherit.so tst-sem-timed-wait.so \
	tst-ttyname.so tst-pthread-barrier.so tst-feexcept.so tst-math.so \
	tst-sigaltstack.so tst-fread.so tst-tcp-cork.so tst-tcp-v6.so \
	tst-calloc.so tst-crypt.so tst-non-fpic.so tst-small-malloc.so \
	tst-getopt.so tst-getopt-pie.so tst-non-pie.so tst-semaphore.so \
	tst-elf-init.so tst-realloc.so tst-setjmp.so \
	libtls.so libtls_gold.so tst-tls.so tst-tls-gold.so tst-tls-pie.so
#	libstatic-thread-variable.so tst-static-thread-variable.so \

#TODO For now let us disable these tests for aarch64 until
# we support floating point numbers, TLS and correct syscall handling
# The tst-ifaddrs.so is an exception and it does not compile due to some
# missing headers
ifeq ($(arch),x64)
tests += tst-syscall.so tst-ifaddrs.so tst-mmx-fpu.so
endif

tests += testrunner.so

# Tests with special compilation parameters needed...
$(out)/tests/tst-mmap.so: COMMON += -Wl,-z,now
$(out)/tests/tst-elf-permissions.so: COMMON += -Wl,-z,relro
$(out)/tests/misc-free-perf.so: COMMON += -faligned-new

# The following tests use special linker trickery which apprarently
# doesn't work as expected with GOLD linker, so we need to choose BFD.
# TODO: figure out why this workaround was needed (the reason may be
# different for each of these tests), and avoid this workaround!
$(out)/tests/tst-mmap.so: COMMON += -fuse-ld=bfd
$(out)/tests/tst-elf-permissions.so: COMMON += -fuse-ld=bfd
$(out)/tests/tst-tls.so: COMMON += -fuse-ld=bfd
$(out)/tests/tst-tls-pie.so: COMMON += -fuse-ld=bfd

$(out)/tests/tst-dlfcn.so: COMMON += -rdynamic -ldl

$(out)/tests/tst-tls.so: \
		$(src)/tests/tst-tls.cc \
		$(out)/tests/libtls.so
	$(makedir)
	$(call quiet, cd $(out); $(CXX) $(CXXFLAGS) $(LDFLAGS) -pthread -D__SHARED_OBJECT__=1 -shared -o $@ $< tests/libtls.so, CXX tests/tst-tls.cc)

$(out)/tests/tst-tls-pie.o: CXXFLAGS:=$(subst -fPIC,-fpie,$(CXXFLAGS))
$(out)/tests/tst-tls-pie.o: $(src)/tests/tst-tls.cc
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) -c -o $@ $<, CXX tests/tst-tls-pie.cc)
$(out)/tests/tst-tls-pie.so: \
		$(out)/tests/tst-tls-pie.o \
		$(out)/tests/libtls.so
	$(makedir)
	$(call quiet, cd $(out); $(CXX) $(CXXFLAGS) $(LDFLAGS) -pthread -fpie -o $@ $< $(LIBS) tests/libtls.so, LD tests/tst-tls-pie.so)

$(out)/tests/libtls_gold.so: COMMON += -fuse-ld=gold
$(out)/tests/libtls_gold.so: $(out)/tests/libtls.o
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -o $@ $< $(LIBS), LD tests/libtls_gold.so)

$(out)/tests/tst-tls-gold.so: COMMON += -fuse-ld=gold
$(out)/tests/tst-tls-gold.so: \
		$(src)/tests/tst-tls.cc \
		$(out)/tests/libtls_gold.so
	$(makedir)
	$(call quiet, cd $(out); $(CXX) $(CXXFLAGS) $(LDFLAGS) -D__SHARED_OBJECT__=1 -shared -o $@ $< tests/libtls_gold.so, CXX tests/tst-tls.cc)

common-boost-tests := tst-vfs.so tst-libc-locking.so misc-fs-stress.so \
	misc-bdev-write.so misc-bdev-wlatency.so misc-bdev-rw.so \
	tst-promise.so tst-dlfcn.so tst-stat.so tst-wait-for.so \
	tst-bsd-tcp1.so tst-bsd-tcp1-zsnd.so tst-bsd-tcp1-zrcv.so \
	tst-bsd-tcp1-zsndrcv.so tst-async.so tst-rcu-list.so tst-tcp-listen.so \
	tst-poll.so tst-bitset-iter.so tst-timer-set.so tst-clock.so \
	tst-rcu-hashtable.so tst-unordered-ring-mpsc.so \
	tst-seek.so tst-ctype.so tst-wctype.so tst-string.so tst-time.so

boost-tests := $(common-boost-tests)

rofs-only-boost-tests := rofs/tst-stdio.so

zfs-only-boost-tests := tst-rename.so tst-stdio.so

boost-tests += $($(fs_type)-only-boost-tests)

$(boost-tests:%=$(out)/tests/%): LIBS += \
	-lboost_unit_test_framework \
	-lboost_filesystem

common-tests := $(tests) $(common-boost-tests)
tests += $(boost-tests)

solaris-tests := tst-solaris-taskq.so

# FIXME: two of the test below can't compile now because of include path
# (BSD and OSv header files get mixed up, etc.).
#zfs-tests := misc-zfs-disk.so misc-zfs-io.so misc-zfs-arc.so
zfs-tests := misc-zfs-io.so 
solaris-tests += $(zfs-tests)

tests += $(solaris-tests)

$(zfs-tests:%=$(out)/tests/%): COMMON+= \
        -DBUILDING_ZFS \
        -I$(src)/bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs \
        -I$(src)/bsd/sys/cddl/contrib/opensolaris/common/zfs 

$(solaris-tests:%=$(out)/tests/%): COMMON+= \
        -Wno-strict-aliasing \
        -Wno-unknown-pragmas \
        -Wno-unused-variable \
        -Wno-unused-but-set-variable \
        -Wno-switch \
        -Wno-maybe-uninitialized \
        -I$(src)/bsd/sys/cddl/contrib/opensolaris/common \
        -I$(src)/bsd/sys/cddl/contrib/opensolaris/uts/common \
        -I$(src)/bsd/sys/cddl/compat/opensolaris \
	-I $(src)/bsd/sys -I $(src)/bsd -I $(src)/bsd/$(ARCH)

all_tests := $(tests:%=tests/%)
all_tests += $(specific-fs-tests:%=tests/%)

build_all_tests: $(all_tests:%=$(out)/%)
.PHONY: build_all_tests clean

# Automatically generate usr.manifest which includes all tests.
usr.manifest: build_all_tests $(lastword $(MAKEFILE_LIST)) usr.manifest.skel FORCE
	@echo "  generating modules/tests/usr.manifest"
	@cat $@.skel > $@
	@if [[ "$(CROSS_PREFIX)" == aarch64* ]]; then \
		./add_aarch64_boost_libraries.sh $(OSV_BASE) >> $@; \
	else \
		LD_LIBRARY_PATH=$(boost-lib-dir) ldd $(addprefix $(out)/tests/,$(boost-tests)) | grep libboost | sed 's/ *[^ ] *\(.*\) => \(.*\) .*/\/usr\/lib\/\1: \2/' | sort | uniq >> $@; \
	fi
	@echo $(all_tests) | tr ' ' '\n' | grep -v "tests/rofs/tst-.*.so" | awk '{print "/" $$0 ": ./" $$0}' >> $@
	@echo $(all_tests) | tr ' ' '\n' | grep "tests/rofs/tst-.*.so" | sed 's/\.so//' | awk 'BEGIN { FS = "/" } ; { print "/tests/" $$3 "-rofs.so: ./tests/" $$2 "/" $$3 ".so"}' >> $@
	$(call very-quiet, ./create_static.sh $(out) usr.manifest $(fs_type))
.PHONY: FORCE
FORCE:

common.manifest: build_all_tests $(lastword $(MAKEFILE_LIST)) usr.manifest.skel FORCE
	@echo "  generating modules/tests/common.manifest"
	@cat usr.manifest.skel > $@
	@if [[ "$(CROSS_PREFIX)" == aarch64* ]]; then \
		./add_aarch64_boost_libraries.sh $(OSV_BASE) >> $@; \
	else \
		LD_LIBRARY_PATH=$(boost-lib-dir) ldd $(addprefix $(out)/tests/,$(boost-tests)) | grep libboost | sed 's/ *[^ ] *\(.*\) => \(.*\) .*/\/usr\/lib\/\1: \2/' | sort | uniq >> $@; \
	fi
	@echo $(common-tests) $(solaris-tests) | tr ' ' '\n' | awk '{print "/tests/" $$0 ": ./tests/" $$0}' >> $@

fs.manifest: build_all_tests $(lastword $(MAKEFILE_LIST)) FORCE
	@echo "  generating modules/tests/fs.manifest"
	@echo $(specific-fs-tests) $($(fs_type)-only-boost-tests) | tr ' ' '\n' | grep -v "rofs/tst-.*.so" | awk '{print "/tests/" $$0 ": ./tests/" $$0}' > $@
	@echo $(specific-fs-tests) $($(fs_type)-only-boost-tests) | tr ' ' '\n' | grep "rofs/tst-.*.so" | sed 's/\.so//' | awk 'BEGIN { FS = "/" } ; { print "/tests/" $$2 "-rofs.so: ./tests/" $$1 "/" $$2 ".so"}' >> $@
	$(call very-quiet, ./create_static.sh $(out) fs.manifest $(fs_type))

clean:
	-rm -f usr.manifest common.manifest fs.manifest

ifneq ($(MAKECMDGOALS),clean)
include $(shell test -d $(out)/tests && find $(out)/tests -name '*.d')
endif
