# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# locale-data: compile glibc's locale SOURCE data (localedata/, from a pinned
# glibc release tarball) with the build host's localedef and ship the COMPILED
# output for a conf_locale=1 OSv image. No glibc C source is committed or
# shipped; the data is a build artifact produced from the pinned sources.
#
# Stage 2 byte-identity check (recorded in the PR): the host localedef (>=
# 2.41) produces output byte-for-byte identical to glibc 2.44's own localedef
# for these locales (both compiling the pinned 2.44 sources), per-category and
# in the archive. So we use the host localedef when it is >= 2.41, and build
# 2.44's localedef only if the host's is older or absent.
#
# Build-line options (reach this Makefile through the environment, like
# conf_zfs and fs_type):
#   conf_locale=1          required; a conf_locale=0 kernel cannot load the
#                          data, so including this module with conf_locale=0 is
#                          a hard build failure.
#   conf_locale_data=14    (default) the 14 default locales as directories
#                          AND in a locale-archive, plus libc.mo for their
#                          languages.
#   conf_locale_data=full  every UTF-8 locale glibc ships, archive form only
#                          (directory form would be thousands of files), plus
#                          libc.mo for every language with a catalog.
#   conf_locale_data=none  nothing (the module installs no data; for images
#                          that supply their own LOCPATH).

GLIBC_VERSION=2.44
GLIBC_TARBALL=glibc-$(GLIBC_VERSION).tar.xz
GLIBC_URL=https://ftp.gnu.org/gnu/glibc/$(GLIBC_TARBALL)
GLIBC_SHA256=37f600f2bef3c5e8300147059568b2a2e40a7ad6ccc65ce942556d49429cc667

conf_locale ?= 0
conf_locale_data ?= 14

DOWNLOAD=download
SRCDIR=src/glibc-$(GLIBC_VERSION)
LOCALEDATA=$(SRCDIR)/localedata
INSTALL=install
LOCALE_DIR=$(INSTALL)/usr/lib/locale
MSG_DIR=$(INSTALL)/usr/share/locale
STAMP=$(INSTALL)/.stamp
MODE_MARKER=.locale-data-mode

# The 14 default locales (STAGE2-SPEC).  Their message-catalog languages: en
# has no catalog (English is the msgid), the rest map to a glibc po file.
DEFAULT_LOCALES=en_US en_GB de_DE fr_FR es_ES it_IT pt_BR nl_NL sv_SE pl_PL ru_RU ja_JP zh_CN ko_KR
DEFAULT_LANGS=de en_GB es fr it ja ko nl pl pt_BR ru sv zh_CN

# localedef: the host's if it is >= 2.41 (byte-identical to 2.44 for this
# data, verified in Stage 2); otherwise build 2.44's from the pinned tree.
HOST_LOCALEDEF_VER := $(shell localedef --version 2>/dev/null | head -1 | sed 's/.* //')
LOCALEDEF := $(shell \
	v="$(HOST_LOCALEDEF_VER)"; \
	maj=$$(echo "$$v" | cut -d. -f1); \
	min=$$(echo "$$v" | cut -d. -f2); \
	if [ -n "$$maj" ] && { [ "$$maj" -gt 2 ] 2>/dev/null || { [ "$$maj" = 2 ] && [ "$$min" -ge 41 ] 2>/dev/null; }; }; then \
		echo localedef; \
	else echo BUILD; fi)

module: $(STAMP)

ifeq ($(conf_locale),1)

$(DOWNLOAD)/$(GLIBC_TARBALL):
	@mkdir -p $(DOWNLOAD)
	cd $(DOWNLOAD) && wget -q -O $(GLIBC_TARBALL) "$(GLIBC_URL)"

# Verify the pin before trusting a byte of it.
$(DOWNLOAD)/$(GLIBC_TARBALL).ok: $(DOWNLOAD)/$(GLIBC_TARBALL)
	echo "$(GLIBC_SHA256)  $(DOWNLOAD)/$(GLIBC_TARBALL)" | sha256sum -c -
	touch $@

# Extract only localedata/ (localedef's input) and po/ (msgfmt's input).
$(LOCALEDATA)/charmaps/UTF-8: $(DOWNLOAD)/$(GLIBC_TARBALL).ok
	@mkdir -p src
	tar xJf $(DOWNLOAD)/$(GLIBC_TARBALL) -C src \
		glibc-$(GLIBC_VERSION)/localedata glibc-$(GLIBC_VERSION)/po
	touch $@

$(STAMP): $(LOCALEDATA)/charmaps/UTF-8 build.sh $(MODE_MARKER) $(lastword $(MAKEFILE_LIST))
	@if [ "$(LOCALEDEF)" = BUILD ]; then \
		echo "locale-data: host localedef '$(HOST_LOCALEDEF_VER)' is < 2.41; building glibc $(GLIBC_VERSION) localedef" >&2; \
		LOCALEDEF=$$(sh build-localedef.sh "$(abspath $(SRCDIR))"); \
	else \
		LOCALEDEF=localedef; \
	fi; \
	sh build.sh "$(abspath $(LOCALEDATA))" "$(abspath $(INSTALL))" "$$LOCALEDEF" \
		"$(conf_locale_data)" "$(CURDIR)" "$(DEFAULT_LOCALES)" "$(DEFAULT_LANGS)"
	touch $@

# A mode marker at the module root (build.sh clears only install/, so this
# survives a rebuild). It is rewritten only when conf_locale_data changed, so a
# changed mode makes it newer than $(STAMP) and the data is recompiled.
$(MODE_MARKER): FORCE
	@if [ "$$(cat $(MODE_MARKER) 2>/dev/null)" != "$(conf_locale_data)" ]; then \
		echo "$(conf_locale_data)" > $(MODE_MARKER); \
	fi

FORCE:

else

$(STAMP):
	@echo "locale-data: this module needs conf_locale=1 (a conf_locale=0 kernel cannot load locale data); rebuild with conf_locale=1 or drop the module" >&2
	@exit 1

endif

clean:
	rm -rf $(INSTALL) src usr.manifest $(MODE_MARKER)

.PHONY: module clean FORCE
