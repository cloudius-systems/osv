# Directories for downloading and building
DOWNLOAD=download
BUILD=build

# Ncurses
NCURSES_VERSION=5.9
NCURSES_FOLDER=ncurses-$(NCURSES_VERSION)
NCURSES_DOWNLOAD=http://ftp.gnu.org/pub/gnu/ncurses/$(NCURSES_FOLDER).tar.gz
NCURSES_ARCHIVE=download/$(NCURSES_FOLDER).tar.gz
NCURSES_BUILD=$(BUILD)/$(NCURSES_FOLDER)

# Build flags (./configure)
CFLAGS=-fPIC -O2
CONF_FLAGS=--without-debug --without-tests --without-cxx-binding \
 --without-ada --disable-tcap-names --with-termlib=tinfo --with-shared

module: $(NCURSES_BUILD)/lib/libncurses.so

$(NCURSES_BUILD)/lib/libncurses.so: $(NCURSES_BUILD)/Makefile
	cd $(NCURSES_BUILD) && $(MAKE)

$(NCURSES_BUILD)/Makefile: $(NCURSES_BUILD)/configure
	cd $(NCURSES_BUILD) && \
		CFLAGS="$(CFLAGS)" ./configure $(CONF_FLAGS)

$(NCURSES_BUILD)/configure: $(NCURSES_ARCHIVE) | $(BUILD)
	cd $(BUILD) && \
		tar xzf ../$(NCURSES_ARCHIVE)
	touch $(NCURSES_BUILD)/configure

$(NCURSES_ARCHIVE): | $(DOWNLOAD)
	cd $(DOWNLOAD) && \
	 curl --remote-name --remote-time $(NCURSES_DOWNLOAD)

$(DOWNLOAD) $(BUILD):
	@mkdir -p $@

clean:
	rm -rf $(BUILD) $(DOWNLOAD)

.PHONY: module clean
