src = $(shell readlink -f ../..)
module-dir = $(src)/modules/lwext4

VERSION_MAJOR = 1
VERSION_MINOR = 0
VERSION_PATCH = 0

VERSION = $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)

BUILD_TYPE = Release

COMMON_DEFINITIONS =                                      \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE)                  \
	-DVERSION_MAJOR=$(VERSION_MAJOR)                  \
	-DVERSION_MINOR=$(VERSION_MINOR)                  \
	-DVERSION_PATCH=$(VERSION_PATCH)                  \
	-DVERSION=$(VERSION)                              \

all: module
module: lwext4

lwext4: upstream/lwext4/build_lib_only/src/liblwext4.so

.PHONY: lwext4

# Pin the lwext4 checkout to an exact commit.  A bare `clone --depth 1` takes
# whatever the remote HEAD happens to be at build time, so two builds of the
# same OSv tree could link different lwext4 revisions and neither would record
# which one.  That makes any ext4 measurement or bug report unreproducible after
# the fork moves.  Override with `make lwext4_commit=<sha>` to test another
# revision.
lwext4_commit?=6f6d8f7c509da696e71e80cc5a59abfc48e4971b

# Fixes not yet in lwext4 or the osvunikernel fork, applied in order to the
# pinned checkout. Each patch says where it comes from; drop it once the
# pinned commit carries the fix. A patch that does not apply fails the build.
lwext4_patches := $(sort $(wildcard $(module-dir)/patches/*.patch))

upstream/lwext4/.git:
	mkdir -p $(module-dir)/upstream && cd $(module-dir)/upstream && \
	git clone https://github.com/osvunikernel/lwext4.git && \
	cd lwext4 && git checkout --detach $(lwext4_commit)

# The stamp records that the patches were applied, so they are applied once
# per checkout. A new or changed patch needs `make clean`.
upstream/lwext4/.patched: $(lwext4_patches) | upstream/lwext4/.git
	cd $(module-dir)/upstream/lwext4 && \
	if [ -e .patched ]; then \
		echo "lwext4: patches changed; run make clean" >&2; exit 1; \
	fi && \
	for p in $(lwext4_patches); do patch -p1 --forward < $$p || exit 1; done && \
	touch .patched

upstream/lwext4/build_lib_only/src/liblwext4.so: upstream/lwext4/.patched
	cd $(module-dir)/upstream/lwext4 && \
	rm -R -f build_lib_only && mkdir build_lib_only && \
	cd build_lib_only && cmake $(COMMON_DEFINITIONS) -DLIB_ONLY=TRUE -DLWEXT4_BUILD_SHARED_LIB=ON .. && make

clean:
	cd $(module-dir) && rm -rf upstream
