#!/bin/sh
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# Re-derive the item tables of libc/locale/locale-data.cc by measuring glibc
# (see item-types.py) and compare them with the code and with
# libc/locale/langinfo-builtin.inc:
#   item-types.sh <test locale data (make-test-locales.sh's out)> <scratch>
# Compiles every UTF-8 locale in /usr/share/i18n/SUPPORTED with the host's
# localedef into <scratch>/all, then runs item-dump, item-min.sh,
# item-names.sh and item-types.py. Exit 1 on any mismatch.
set -e
data=$1; t=$2; here=$(cd "$(dirname "$0")" && pwd); tree=$(cd "$here/../../.." && pwd)
mkdir -p "$t/all"
grep ' UTF-8$' /usr/share/i18n/SUPPORTED | awk '{print $1}' | sed 's/\.UTF-8//' |
    xargs -P 16 -I{} sh -c 'n={}; b=${n%%@*}; m=${n#$b}; [ -d "'"$t"'/all/$b.utf8$m" ] ||
        localedef -i $b$m -f UTF-8 "'"$t"'/all/$b.utf8$m" >/dev/null 2>&1 || echo "localedef $n failed" >&2'
cc -O1 -Wall -o "$t/item-dump" "$here/item-dump.c"
names=$( (cd "$data/dir" && ls -d *.utf8 xx_L1) ; (cd "$t/all" && ls -d *.utf8*) )
echo "locales: $(echo $names | wc -w) ($(ls -d "$t"/all/*.utf8* | wc -l) from /usr/share/i18n/SUPPORTED)"
LOCPATH="$data/dir:$t/all" "$t/item-dump" C C.UTF-8 $names > "$t/dump.txt"
sh "$here/item-min.sh" "$data/dir/de_DE.utf8" "$t/min" > "$t/min.txt"
sh "$here/item-names.sh" "$t/names" > "$t/names.txt"
python3 "$here/item-types.py" "$t/dump.txt" "$t/min.txt" "$tree/libc/locale/locale-data.cc" \
    "$tree/libc/locale/langinfo-builtin.inc" "$t/names.txt"
