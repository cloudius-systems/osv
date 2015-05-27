# Directories for downloading and building
DOWNLOAD=download
BUILD=build
OUT=out

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

MAIN=$(NCURSES_BUILD)/lib/libncurses.so
TI_VT100=$(OUT)/terminfo/v/vt100-qemu

module: $(MAIN) $(TI_VT100)

$(MAIN): $(NCURSES_BUILD)/Makefile
	cd $(NCURSES_BUILD) && $(MAKE)

$(NCURSES_BUILD)/Makefile: $(NCURSES_BUILD)/configure
	cd $(NCURSES_BUILD) && \
		CFLAGS="$(CFLAGS)" ./configure $(CONF_FLAGS)

$(NCURSES_BUILD)/configure: $(NCURSES_ARCHIVE) | $(BUILD)
	cd $(BUILD) && \
		tar xzf ../$(NCURSES_ARCHIVE)
	cd $(NCURSES_BUILD); patch -p1 < ../../ncurses-5.9-gcc-5.patch
	touch $(NCURSES_BUILD)/configure

$(NCURSES_ARCHIVE): | $(DOWNLOAD)
	cd $(DOWNLOAD) && \
	 curl --remote-name --remote-time $(NCURSES_DOWNLOAD)

$(TI_VT100): $(MAIN)
	@mkdir -p $(OUT)/terminfo
	infocmp vt100 | sed 's/cud1=^J,/cud1=\\E[B,/; s/ind=^J,/ind=\\E[S,/; s/vt100/vt100-qemu/' \
		> $(OUT)/terminfo.tmp
	tic -o $(OUT)/terminfo $(OUT)/terminfo.tmp
	rm -f $(OUT)/terminfo.tmp

$(DOWNLOAD) $(BUILD) $(OUT):
	@mkdir -p $@

clean:
	rm -rf $(BUILD) $(DOWNLOAD) $(OUT)

.PHONY: module clean
