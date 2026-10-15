#!/bin/sh
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# Compile the test locales with the build host's localedef (glibc's locales
# package) for an OSv test image built with conf_locale=1:
#   make-test-locales.sh <out>
# <out>/dir/<l>.utf8             the 14 default locales and tr_TR fa_IR ar_SA my_MM
#                                cs_CZ fr_CA, directory form (LOCPATH)
# <out>/dir/de_DE.ISO-8859-1     UTF-8 data under a Latin-1 name (codeset mismatch)
# <out>/fb/de.utf8 de@euro de_AT de_CH.utf8@euro   sv_SE it_IT fr_FR es_ES data under
#                                shorter names (the lookup fallback order)
# <out>/dir/xx_L1                de_DE in ISO-8859-1 (a locale that is not UTF-8)
# <out>/arch/usr/lib/locale/locale-archive   the same locales as an archive
# <out>/default/da_DK.utf8       for the default directory /usr/lib/locale
# <out>/localedef-version.txt
# modules/tests runs it only for conf_locale=1 test images. It exits 1, with
# a message, if localedef is missing or cannot compile a locale (glibc's
# locale sources, the "locales" package, are needed): tst-locale-data on a
# conf_locale=1 image fails without the data.
set -e
out=$1
command -v localedef >/dev/null 2>&1 || {
    echo "make-test-locales: no localedef; conf_locale=1 test images need glibc's localedef and locale sources" >&2
    exit 1
}
tmp=$out.tmp
rm -rf "$tmp"
mkdir -p "$tmp/dir" "$tmp/arch/usr/lib/locale" "$tmp/default"
# The 14 default locales, then tr_TR (case mapping) and fa_IR ar_SA my_MM
# (alternative digits, right-to-left formats), which are not default locales,
# and cs_CZ (the contraction "ch") and fr_CA (backward accents) for collation
# (Stage 1c).
for l in en_US en_GB de_DE fr_FR es_ES it_IT pt_BR nl_NL sv_SE pl_PL ru_RU ja_JP zh_CN ko_KR \
         tr_TR fa_IR ar_SA my_MM cs_CZ fr_CA; do
    localedef -i $l -f UTF-8 "$tmp/dir/$l.utf8" >/dev/null 2>&1 || {
        echo "make-test-locales: localedef -i $l failed (are glibc's locale sources installed?)" >&2
        exit 1
    }
    localedef --prefix="$tmp/arch" --add-to-archive "$tmp/dir/$l.utf8" >/dev/null
done
cp -r "$tmp/dir/de_DE.utf8" "$tmp/dir/de_DE.ISO-8859-1"
# Lookup fallback (tst-locale-data): fb/ has a different locale under each
# shorter name, so the one found shows the order glibc searches.
mkdir -p "$tmp/fb"
cp -r "$tmp/dir/sv_SE.utf8" "$tmp/fb/de.utf8"
cp -r "$tmp/dir/it_IT.utf8" "$tmp/fb/de@euro"
cp -r "$tmp/dir/fr_FR.utf8" "$tmp/fb/de_AT"
cp -r "$tmp/dir/es_ES.utf8" "$tmp/fb/de_CH.utf8@euro"
localedef -i de_DE -f ISO-8859-1 "$tmp/dir/xx_L1" >/dev/null 2>&1 || true
localedef -i da_DK -f UTF-8 "$tmp/default/da_DK.utf8" >/dev/null 2>&1 || true
for f in dir/xx_L1/LC_CTYPE default/da_DK.utf8/LC_CTYPE; do
    [ -s "$tmp/$f" ] || { echo "localedef: no $f" >&2; exit 1; }
done
localedef --version | head -1 > "$tmp/localedef-version.txt"
rm -rf "$out"
mv "$tmp" "$out"
