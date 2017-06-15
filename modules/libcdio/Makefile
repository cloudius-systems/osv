# A small Makefile "download and build" for libcdio

# Directories for downloading and building
DOWNLOAD=download
BUILD=build

# libcdio
LIBCDIO_VERSION=0.94
LIBCDIO_FOLDER=libcdio-$(LIBCDIO_VERSION)
LIBCDIO_DOWNLOAD=https://ftp.gnu.org/gnu/libcdio/libcdio-$(LIBCDIO_VERSION).tar.gz
LIBCDIO_ARCHIVE=download/$(LIBCDIO_FOLDER).tar.gz
LIBCDIO_BUILD=$(BUILD)/$(LIBCDIO_FOLDER)

CFLAGS:=-fPIC
# libtool would ignore -shared anyway
# We should set LDFLAGS=-shared. But then configure stage fails with 
# "configure: error: C compiler cannot create executables".
# If we later edit Makefile to set LDFLAGS=-shared, then libtool removes it.
# So, we will capture required cmd from make V=1, and relink the binary.
LDFLAGS:=

MAIN=$(LIBCDIO_BUILD)/src/.libs/iso-read

module: $(MAIN) $(MAIN).so

$(MAIN): $(LIBCDIO_BUILD)/Makefile
	cd $(LIBCDIO_BUILD) && make

$(MAIN).so: $(MAIN)
	./relink-binary.sh $@

$(LIBCDIO_BUILD)/Makefile: $(LIBCDIO_BUILD)/configure
	cd $(LIBCDIO_BUILD) && CFLAGS=$(CFLAGS) LDFLAGS=$(LDFLAGS) ./configure

$(LIBCDIO_BUILD)/configure: $(LIBCDIO_ARCHIVE) | $(BUILD)
	cd $(BUILD) && tar xzf ../$(LIBCDIO_ARCHIVE)
	cd $(LIBCDIO_BUILD) && patch -p1 < ../../exit.patch
	touch $(LIBCDIO_BUILD)/configure

$(LIBCDIO_ARCHIVE): | $(DOWNLOAD)
	cd $(DOWNLOAD) && \
		curl --remote-name --remote-time $(LIBCDIO_DOWNLOAD)

$(DOWNLOAD) $(BUILD):
	@mkdir -p $@

clean:
	rm -rf $(BUILD) $(DOWNLOAD)



.PHONY: module clean
