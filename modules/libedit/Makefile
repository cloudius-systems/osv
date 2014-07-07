# A small Makefile "download and build" for libedit

# Directories for downloading and building
DOWNLOAD=download
BUILD=build

# libedit
LE_VERSION=20140620-3.1
LE_FOLDER=libedit-$(LE_VERSION)
LE_DOWNLOAD=http://thrysoee.dk/editline/libedit-$(LE_VERSION).tar.gz
LE_ARCHIVE=download/$(LE_FOLDER).tar.gz
LE_BUILD=$(BUILD)/$(LE_FOLDER)

CFLAGS:=-fPIC

MAIN=$(LE_BUILD)/src/.libs/libedit.so.0

module: $(MAIN)

$(MAIN): $(LE_BUILD)/Makefile
	cd $(LE_BUILD) && make

$(LE_BUILD)/Makefile: $(LE_BUILD)/configure
	cd $(LE_BUILD) && ./configure

$(LE_BUILD)/configure: $(LE_ARCHIVE) | $(BUILD)
	cd $(BUILD) && tar xzf ../$(LE_ARCHIVE)
	touch $(LE_BUILD)/configure

$(LE_ARCHIVE): | $(DOWNLOAD)
	cd $(DOWNLOAD) && \
		curl --remote-name --remote-time $(LE_DOWNLOAD)

$(DOWNLOAD) $(BUILD):
	@mkdir -p $@

clean:
	rm -rf $(BUILD) $(DOWNLOAD)

.PHONY: module clean
