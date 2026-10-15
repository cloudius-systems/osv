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

upstream/lwext4/.git:
	mkdir -p $(module-dir)/upstream && cd $(module-dir)/upstream && \
	git clone https://github.com/osvunikernel/lwext4.git && \
	cd lwext4 && git checkout --detach $(lwext4_commit)

upstream/lwext4/build_lib_only/src/liblwext4.so: upstream/lwext4/.git
	cd $(module-dir)/upstream/lwext4 && \
	rm -R -f build_lib_only && mkdir build_lib_only && \
	cd build_lib_only && cmake $(COMMON_DEFINITIONS) -DLIB_ONLY=TRUE -DLWEXT4_BUILD_SHARED_LIB=ON .. && make

clean:
	cd $(module-dir) && rm -rf upstream
