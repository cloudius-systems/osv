#!/bin/sh
# Copyright (C) 2026 Greg Burd.
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# mk-cats.sh <out>: test catalogs for the gettext probes and differential
# (msgfmt from gettext-tools: test-only, never shipped in OSv).
#   <out>/<name>/LC_MESSAGES/where.mo   one per candidate directory name; "where" -> the name
#   <out>/<lang>/LC_MESSAGES/t1.mo      Plural-Forms, msgctxt, 300 extra keys, hash table
#   <out>/<lang>/LC_MESSAGES/t1n.mo     the same, --no-hash
#   <out>/<lang>/LC_MESSAGES/t4.mo      the same, big-endian
#   <out>/<lang>/LC_MESSAGES/t2.mo      no Plural-Forms (default rule)
#   <out>/<lang>/LC_MESSAGES/t3.mo      ISO-8859-1 catalog
#   <out>/<lang>/LC_MESSAGES/t5.mo      deeply nested Plural-Forms expression
set -e
O=$1; mkdir -p "$O"; T=$(mktemp -d)
hdr() { printf 'msgid ""\nmsgstr ""\n"Content-Type: text/plain; charset=%s\\n"\n%s\n' "$1" "$2"; }
for n in de_AT.UTF-8@euro de_AT.utf8@euro de_AT@euro de.UTF-8@euro de.utf8@euro de@euro \
         de_AT.UTF-8 de_AT.utf8 de_AT de.UTF-8 de.utf8 de de_AT.ISO-8859-1 de_AT.iso88591 \
         de.ISO-8859-1 de.iso88591 xx_YY.UTF-8 xx_YY xx C POSIX C.UTF-8 C.utf8 en en_US \
         fr fr_FR.UTF-8 sv; do
  { hdr UTF-8 ''; printf 'msgid "where"\nmsgstr "%s"\n' "$n"; } > $T/w.po
  mkdir -p "$O/$n/LC_MESSAGES"; msgfmt -o "$O/$n/LC_MESSAGES/where.mo" $T/w.po
done
for l in de ru; do
  if [ $l = ru ]; then pf='nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);'
  else pf='nplurals=2; plural=(n != 1);'; fi
  { hdr UTF-8 "\"Plural-Forms: $pf\\\\n\""
    cat <<PO
msgid "hello"
msgstr "$l:hallo"

msgid "Gr\303\274\303\237e"
msgstr "$l:Gr\303\274\303\237e \342\202\254"

msgid "file"
msgid_plural "files"
msgstr[0] "$l:datei0"
msgstr[1] "$l:datei1"
msgstr[2] "$l:datei2"

msgctxt "menu"
msgid "Open"
msgstr "$l:menu-oeffnen"

msgctxt "verb"
msgid "Open"
msgstr "$l:verb-oeffnen"

msgid "Open"
msgstr "$l:oeffnen"

msgid "empty"
msgstr ""

#, fuzzy
msgid "fuzzy"
msgstr "$l:fuzzy-ignored"

msgid "pl-empty"
msgid_plural "pl-empties"
msgstr[0] "$l:pe0"
msgstr[1] ""
msgstr[2] "$l:pe2"

PO
    i=0; while [ $i -lt 300 ]; do printf 'msgid "k%d"\nmsgstr "%s:v%d"\n\n' $i $l $i; i=$((i+1)); done
  } > $T/t1.po
  mkdir -p "$O/$l/LC_MESSAGES"
  msgfmt -o "$O/$l/LC_MESSAGES/t1.mo" $T/t1.po
  msgfmt --no-hash -o "$O/$l/LC_MESSAGES/t1n.mo" $T/t1.po
  msgfmt --endianness=big -o "$O/$l/LC_MESSAGES/t4.mo" $T/t1.po
  { hdr UTF-8 ''; printf 'msgid "file"\nmsgid_plural "files"\nmsgstr[0] "%s:d0"\nmsgstr[1] "%s:d1"\n\nmsgid "hello"\nmsgstr "%s:t2hallo"\n' $l $l $l; } > $T/t2.po
  msgfmt -o "$O/$l/LC_MESSAGES/t2.mo" $T/t2.po
  { hdr ISO-8859-1 '"Plural-Forms: nplurals=2; plural=n != 1;\\n"'; printf 'msgid "Gruesse"\nmsgstr "%s:Gr\374\337e"\n\nmsgid "file"\nmsgid_plural "files"\nmsgstr[0] "%s:D\344tei"\nmsgstr[1] "%s:D\344teien"\n' $l $l $l; } > $T/t3.po
  msgfmt -o "$O/$l/LC_MESSAGES/t3.mo" $T/t3.po
  { hdr UTF-8 '"Plural-Forms: nplurals=2; plural=((((((((((((((((((((((((((((((n))))))))))))))))))))))))))))));\\n"'; printf 'msgid "file"\nmsgid_plural "files"\nmsgstr[0] "%s:b0"\nmsgstr[1] "%s:b1"\n' $l $l; } > $T/t5.po
  msgfmt -o "$O/$l/LC_MESSAGES/t5.mo" $T/t5.po
done
find $T -delete
