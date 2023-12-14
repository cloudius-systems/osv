module: usr.manifest common.manifest fs.manifest build_all_tests

include ../common.gmk

# Ask make to not delete "intermediate" results, such as the .o in the chain
# .cc -> .o -> . Otherwise, during the first build, make considers the .o
# to be intermediate, and deletes it, but the newly-created ".d" files lists
# the ".o" as a target - so it needs to be created again on the second make.
# See commit fac05c95 for a longer explanation.
.SECONDARY:

COMMON = -g -O2 -fpie -DBOOST_TEST_DYN_LINK \
	-U _FORTIFY_SOURCE -Wall -Wformat=0 -Wno-pointer-arith

INCLUDES =

LIBS = $(libgcc_s_dir)/libgcc_s.so.1

CXXFLAGS = -std=gnu++11 $(COMMON)
CFLAGS = -std=gnu99 $(COMMON)

$(out)/tests-with-linux-ld/%.o: $(src)/tests/%.cc
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) -c -o $@ $<, CXX $*.cc)
$(out)/tests-with-linux-ld/%.o: $(src)/tests/%.c
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -c -o $@ $< , CC $*.c)
$(out)/tests-with-linux-ld/rofs/%.o: $(src)/tests/%.cc
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) -DREAD_ONLY_FS -c -o $@ $<, CXX $*.cc)
$(out)/tests-with-linux-ld/rofs/%.o: $(src)/tests/%.c
	$(makedir)
	$(call quiet, $(CC) $(CFLAGS) -DREAD_ONLY_FS -c -o $@ $< , CC $*.c)
$(out)/tests-with-linux-ld/%: $(out)/tests-with-linux-ld/%.o
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(LIBS), LD $*)
$(out)/%.so: $(out)/%.o
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -o $@ $< $(LIBS), LD $*.so)

$(out)/tests-with-linux-ld/tst-non-fpic.o: CXXFLAGS:=$(subst -fpie,-fno-pic -mcmodel=large,$(CXXFLAGS))

$(out)/tests-with-linux-ld/tst-non-pie: CXXFLAGS:=$(subst -fpie,-no-pie,$(CXXFLAGS))
$(out)/tests-with-linux-ld/tst-non-pie: $(src)/tests/tst-non-pie.cc
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(LIBS), LD tests-with-linux-ld/tst-non-pie)

$(out)/tests-with-linux-ld/tst-reloc.o: CFLAGS:=$(subst -fPIC,-fpie,$(CFLAGS))
$(out)/tests-with-linux-ld/tst-reloc: $(src)/tests/tst-reloc.c
	$(call quiet, $(CC) $(CFLAGS) $(LDFLAGS) -pie -o $@ $< $(LIBS), LD tests-with-linux-ld/tst-reloc)

$(out)/tests-with-linux-ld/tst-bitset-iter.o: COMMON:=-I $(src)/include $(COMMON)
$(out)/tests-with-linux-ld/tst-queue-mpsc.o: COMMON:=-I $(src)/include -DLINUX $(COMMON)
$(out)/tests-with-linux-ld/tst-poll.o: COMMON:=-I $(src)/include $(COMMON)
$(out)/tests-with-linux-ld/rofs/tst-readdir.o: COMMON:=-Wno-deprecated-declarations $(COMMON)
$(out)/tests-with-linux-ld/tst-tcp-listen.o: COMMON:=-I $(src)/include $(COMMON)

$(out)/tests-with-linux-ld/misc-fsx.o: COMMON:=-D__OSV__ $(COMMON)
$(out)/tests-with-linux-ld/tst-console.o: COMMON:=-D__OSV__ $(COMMON)
$(out)/tests-with-linux-ld/tst-fallocate.o: COMMON:=-D__OSV__ $(COMMON)
$(out)/tests-with-linux-ld/tst-fread.o: COMMON:=-D__OSV__ $(COMMON)
$(out)/tests-with-linux-ld/tst-net_if_test.o: COMMON:=-D__OSV__ $(COMMON)
$(out)/tests-with-linux-ld/tst-pipe.o: COMMON:=-D__OSV__ $(COMMON)
$(out)/tests-with-linux-ld/tst-semaphore.o: COMMON:=-D__DISABLE_NAMED_SEMAPHORES__ $(COMMON)
$(out)/tests-with-linux-ld/tst-uio.o: COMMON:=-D__OSV__ $(COMMON)

#
# These 4 tests are compiled from the same source files but passed in READ_ONLY_FS
# to switch relevant logic in those tests to exercise scenarios applicable
# to read-only filesystem
rofs-only-tests := rofs/tst-chdir rofs/tst-symlink rofs/tst-readdir rofs/tst-concurrent-read

zfs-only-tests := tst-readdir tst-fallocate tst-concurrent-read

specific-fs-tests := $($(fs_type)-only-tests)

tests := tst-pthread misc-ramdisk tst-vblk \
	misc-console misc-readbench misc-mmap-anon-perf misc-loadbalance \
	tst-mmap-file misc-mmap-big-file tst-mmap tst-elf-permissions \
	tst-queue-mpsc tst-af-local tst-pipe tst-yield \
	misc-ctxsw tst-read tst-symlink tst-openat \
	tst-eventfd tst-remove tst-epoll \
	misc-fsx tst-sleep tst-resolve tst-except tst-tcp-nbwrite tst-console \
	misc-setpriority misc-timeslice misc-tls misc-gtod \
	tst-dns-resolver tst-kill tst-truncate \
	misc-panic tst-utimes tst-utimensat tst-futimesat \
	misc-tcp tst-strerror_r misc-random misc-urandom \
	tst-threadcomplete tst-timerfd \
	tst-memmove tst-pthread-clock misc-procfs \
	tst-chdir tst-chmod tst-hello misc-concurrent-io \
	tst-concurrent-init tst-shm \
	tst-align tst-cxxlocale misc-tcp-close-without-reading \
	tst-sigwait misc-malloc misc-memcpy misc-printf tst-hostname \
	tst-sendfile tst-uio tst-printf \
	tst-pthread-affinity tst-pthread-tsd tst-thread-local \
	tst-regex tst-tcp-siocoutq \
	tst-select-timeout tst-faccessat tst-fstatat tst-fcntl misc-mutex2 \
	tst-pthread-setcancelstate tst-pthread-affinity-inherit \
	tst-pthread-barrier tst-math \
	tst-sigaltstack tst-fread tst-tcp-cork tst-tcp-v6 \
	tst-calloc tst-non-fpic tst-feexcept tst-getopt tst-non-pie \
	tst-semaphore tst-elf-init tst-realloc tst-setjmp \
	libtls.so libtls_gold.so tst-tls tst-tls-gold \
	tst-sigaction tst-syscall tst-ifaddrs tst-getdents \
	tst-netlink tst-pthread-create misc-futex-perf \
	misc-syscall-perf tst-reloc misc-vdso-perf misc-scheduler

ifeq ($(arch),x64)
tests += tst-mmx-fpu
endif

# Tests with special compilation parameters needed...
$(out)/tests-with-linux-ld/tst-mmap: COMMON += -Wl,-z,now
$(out)/tests-with-linux-ld/tst-elf-permissions: COMMON += -Wl,-z,relro

# The following tests use special linker trickery which apprarently
# doesn't work as expected with GOLD linker, so we need to choose BFD.
# TODO: figure out why this workaround was needed (the reason may be
# different for each of these tests), and avoid this workaround!
$(out)/tests-with-linux-ld/tst-mmap: COMMON += -fuse-ld=bfd
$(out)/tests-with-linux-ld/tst-elf-permissions: COMMON += -fuse-ld=bfd
$(out)/tests-with-linux-ld/tst-tls: COMMON += -fuse-ld=bfd

$(out)/tests-with-linux-ld/tst-dlfcn: COMMON += -rdynamic -ldl -DLINUX
$(out)/tests-with-linux-ld/tst-utimensat: COMMON += -DLINUX

$(out)/tests-with-linux-ld/libtls.o: CXXFLAGS:=$(subst -fpie,-fPIC,$(CXXFLAGS))

$(out)/tests-with-linux-ld/tst-tls: \
		$(src)/tests/tst-tls.cc \
		$(out)/tests-with-linux-ld/libtls.so
	$(makedir)
	$(call quiet, cd $(out); $(CXX) $(CXXFLAGS) $(LDFLAGS) -pthread -D__SHARED_OBJECT__=1 -o $@ $< tests-with-linux-ld/libtls.so, CXX tests/tst-tls.cc)

$(out)/tests-with-linux-ld/libtls_gold.so: COMMON += -fuse-ld=gold
$(out)/tests-with-linux-ld/libtls_gold.so: $(out)/tests-with-linux-ld/libtls.o
	$(makedir)
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -o $@ $< $(LIBS), LD tests-with-linux-ld/libtls_gold.so)

$(out)/tests-with-linux-ld/tst-tls-gold: COMMON += -fuse-ld=gold
$(out)/tests-with-linux-ld/tst-tls-gold: \
		$(src)/tests/tst-tls.cc \
		$(out)/tests-with-linux-ld/libtls_gold.so
	$(makedir)
	$(call quiet, cd $(out); $(CXX) $(CXXFLAGS) $(LDFLAGS) -D__SHARED_OBJECT__=1 -o $@ $< tests-with-linux-ld/libtls_gold.so, CXX tests/tst-tls.cc)

common-boost-tests := tst-libc-locking misc-fs-stress \
	tst-promise tst-dlfcn tst-stat tst-bsd-tcp1 tst-tcp-listen \
	tst-poll tst-bitset-iter \
	tst-seek tst-ctype tst-wctype tst-string tst-time \
	tst-net_if_test

boost-tests := $(common-boost-tests)

rofs-only-boost-tests := rofs/tst-stdio

zfs-only-boost-tests := tst-rename tst-stdio

boost-tests += $($(fs_type)-only-boost-tests)

$(boost-tests:%=$(out)/tests-with-linux-ld/%): LIBS += \
	-lboost_unit_test_framework \
	-lboost_filesystem

boost-program-options-tests := misc-tcp
$(boost-program-options-tests:%=$(out)/tests-with-linux-ld/%): LIBS += \
	-lboost_program_options

common-tests := $(tests) $(common-boost-tests)
tests += $(boost-tests)

all_tests := $(tests:%=tests-with-linux-ld/%)
all_tests += $(specific-fs-tests:%=tests-with-linux-ld/%)

build_all_tests: $(all_tests:%=$(out)/%)
.PHONY: build_all_tests clean

# Automatically generate usr.manifest which includes all tests.
usr.manifest: build_all_tests $(lastword $(MAKEFILE_LIST)) usr.manifest.skel FORCE
	@echo "  generating modules/tests-with-linux-ld/usr.manifest"
	@cat $@.skel > $@
	@case "$(CROSS_PREFIX)" in \
		"aarch64"*) ../tests/add_aarch64_boost_libraries.sh $(OSV_BASE) >> $@ ;; \
		*) LD_LIBRARY_PATH=$(boost-lib-dir) ldd $(addprefix $(out)/tests-with-linux-ld/,$(boost-tests)) $(addprefix $(out)/tests-with-linux-ld/,$(boost-program-options-tests)) | grep libboost | sed 's/ *[^ ] *\(.*\) => \(.*\) .*/\/usr\/lib64\/\1: \2/' | sort | uniq >> $@ ;; \
	esac
	@echo $(all_tests) | tr ' ' '\n' | grep -v "tests-with-linux-ld/rofs/tst-.*" | awk '{print "/" $$0 ": ./" $$0}' | sed 's/^\/tests-with-linux-ld/\/tests/' >> $@
	@echo $(all_tests) | tr ' ' '\n' | grep "tests-with-linux-ld/rofs/tst-.*" | awk 'BEGIN { FS = "/" } ; { print "/tests/" $$3 "-rofs: ./tests-with-linux-ld/" $$2 "/" $$3 ""}' >> $@
	@$(OSV_BASE)/scripts/manifest_from_host.sh $(out)/tests-with-linux-ld/tst-threadcomplete >> $@
	$(call very-quiet, ./create_static.sh $(out) usr.manifest $(fs_type))
.PHONY: FORCE
FORCE:

common.manifest: build_all_tests $(lastword $(MAKEFILE_LIST)) usr.manifest.skel FORCE
	@echo "  generating modules/tests-with-linux-ld/common.manifest"
	@cat usr.manifest.skel > $@
	@case "$(CROSS_PREFIX)" in \
		"aarch64"*) ../tests/add_aarch64_boost_libraries.sh $(OSV_BASE) >> $@ ;; \
		*) LD_LIBRARY_PATH=$(boost-lib-dir) ldd $(addprefix $(out)/tests-with-linux-ld/,$(boost-tests)) $(addprefix $(out)/tests-with-linux-ld/,$(boost-program-options-tests)) | grep libboost | sed 's/ *[^ ] *\(.*\) => \(.*\) .*/\/usr/\/lib64\/\1: \2/' | sort | uniq >> $@ ;; \
	esac
	@echo $(common-tests) | tr ' ' '\n' | awk '{print "/tests/" $$0 ": ./tests/" $$0}' >> $@

fs.manifest: build_all_tests $(lastword $(MAKEFILE_LIST)) FORCE
	@echo "  generating modules/tests-with-linux-ld/fs.manifest"
	@echo $(specific-fs-tests) $($(fs_type)-only-boost-tests) | tr ' ' '\n' | grep -v "rofs/tst-.*" | awk '{print "/tests/" $$0 ": ./tests-with-linux-ld/" $$0}' > $@
	@echo $(specific-fs-tests) $($(fs_type)-only-boost-tests) | tr ' ' '\n' | grep "rofs/tst-.*" | awk 'BEGIN { FS = "/" } ; { print "/tests/" $$2 "-rofs: ./tests-with-linux-ld/" $$1 "/" $$2 ""}' >> $@
	$(call very-quiet, ./create_static.sh $(out) fs.manifest $(fs_type))

clean:
	-rm -f usr.manifest common.manifest fs.manifest

ifneq ($(MAKECMDGOALS),clean)
include $(shell test -d $(out)/tests-with-linux-ld && find $(out)/tests-with-linux-ld -name '*.d')
endif
