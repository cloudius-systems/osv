quiet = $(if $V, $1, @echo " $2"; $1)

UNFS_URL="https://github.com/benoit-canet/unfsd.git"

autodepend = -MD -MT $@ -MP

src := $(OSV_BASE)
out := $(OSV_BUILD_PATH)
arch := $(ARCH)

INCLUDES = -I$(src)/arch/$(ARCH) -I$(src) -I$(src)/include \
	-I$(src)/arch/common -isystem $(src)/include/glibc-compat \
	-isystem $(src)/include/api -isystem $(src)/include/api/$(ARCH) \
	-isystem $(out)/gen/include

CORE = $(autodepend) $(INCLUDES) -g -O2 -fPIC
COMMON = $(CORE) -DBOOST_TEST_DYN_LINK \
	-U _FORTIFY_SOURCE -D_KERNEL -D__OSV__ -DCONF_debug_memory=0 \
	-Wall -Wno-pointer-arith -Wformat=0 -Wno-format-security

CXXFLAGS = -std=gnu++11 $(COMMON)
CFLAGS = -std=gnu99 $(COMMON)

module: unfsd.bin tst-nfs fsx-linux

unfsd.bin:
	$(call quiet, git clone $(UNFS_URL)) && \
	$(call quiet, cd unfsd) && \
	$(call quiet, ./configure) && \
	$(call quiet, make)
	$(call quiet, cp unfsd/unfsd unfsd.bin)

tst-nfs:
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o tst-nfs.so tst-nfs.cc, LINK $@)

fsx-linux:
	$(call quiet, $(CC)  $(CORE) -shared -o fsx-linux.so fsx-linux.c, LINK $@)
