quiet = $(if $V, $1, @echo " $2"; $1)

UNFS_URL="https://github.com/unfs3/unfs3.git"

autodepend = -MD -MT $@ -MP

src = $(shell readlink -f ../..)
arch := $(ARCH)

INCLUDES = -I../../include
INCLUDES += -I../../arch/$(ARCH) -I../.. -I../../build/$(mode)/gen/include
INCLUDES += -isystem ../../include/glibc-compat
INCLUDES += $(shell $(CXX) -E -xc++ - -v </dev/null 2>&1 | awk '/^End/ {exit} /^ .*c\+\+/ {print "-isystem" $$0}')
INCLUDES += -isystem ../../include/api -isystem ../../include/api/$(ARCH) -isystem ../../build/$(mode)/gen/include
INCLUDES += -isystem ../../bsd/sys -isystem ../../bsd/ -isystem ../../bsd/$(ARCH)

CORE = $(autodepend) $(INCLUDES) -g -O2 -fPIC
COMMON = $(CORE) -DBOOST_TEST_DYN_LINK \
	-U _FORTIFY_SOURCE -D_KERNEL -D__OSV__ -DCONF_debug_memory=0 \
	-Wall -Wno-pointer-arith -Wformat=0 -Wno-format-security

CXXFLAGS = -std=gnu++11 $(COMMON)
CFLAGS = -std=gnu99 $(COMMON)

module: unfsd.bin tst-nfs fsx-linux

unfsd.bin:
	$(call quiet, mkdir -p upstream) && \
	$(call quiet, git clone $(UNFS_URL) upstream/unfs3) && \
	$(call quiet, cd upstream/unfs3) && \
	$(call quiet, ./bootstrap) && \
	$(call quiet, ./configure) && \
	$(call quiet, make)
	$(call quiet, cp upstream/unfs3/unfsd unfsd.bin)

tst-nfs:
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o tst-nfs.so tst-nfs.cc, LINK $@)

fsx-linux:
	$(call quiet, $(CC)  $(CORE) -shared -o fsx-linux.so fsx-linux.c, LINK $@)

check:
	cd $(src) && \
	make image=nfs,nfs-tests && \
	PYTHONPATH=$(src)/scripts modules/nfs-tests/test.py

clean:
	$(call quiet, rm -rf upstream, RM upstream)
	$(call quiet, rm -f *.so unfsd.bin *.d, RM *.so)
