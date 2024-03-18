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

upstream/lwext4/.git:
	mkdir -p $(module-dir)/upstream && cd $(module-dir)/upstream && \
	git clone --depth 1 https://github.com/osvunikernel/lwext4.git

upstream/lwext4/build_lib_only/src/liblwext4.so: upstream/lwext4/.git
	cd $(module-dir)/upstream/lwext4 && \
	rm -R -f build_lib_only && mkdir build_lib_only && \
	cd build_lib_only && cmake $(COMMON_DEFINITIONS) -DLIB_ONLY=TRUE -DLWEXT4_BUILD_SHARED_LIB=ON .. && make

clean:
	cd $(module-dir) && rm -rf upstream
