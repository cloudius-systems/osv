#!/bin/sh
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# Every item name of the installed <langinfo.h> (its enum, after the
# preprocessor) with its category and index: item-names.sh <scratch> > names.txt
set -e
t=$1; here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$t"
printf '#define _GNU_SOURCE\n#include <langinfo.h>\n' | cc -E -P - |
    sed -n 's/^ *\([A-Za-z_][A-Za-z0-9_]*\) *[=,].*/N(\1)/p' | grep -v '_LAST)$\|^N(_NL_NUM)$' > "$t/item-names.h"
cc -O1 -I"$t" -o "$t/item-names" "$here/item-names.c"
"$t/item-names"
