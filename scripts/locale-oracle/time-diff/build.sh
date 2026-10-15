#!/bin/sh
# Copyright (C) 2026 Greg Burd. This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
# Build OSv's libc/time and libc/locale C files on Linux for the host differentials (sfd, spd, fmd)
# and the loader fuzz harness (fuzz-locale-data). The OSv files are compiled with the shim (their
# names prefixed osv_); the harnesses are compiled without it, so they call glibc's functions
# directly. OSV_TREE is the OSv tree the files are taken from (default: the one this script is in).
set -e
cd "$(dirname "$0")"; T=${OSV_TREE:-$(cd ../../.. && pwd)}; mkdir -p src o
for f in libc/time/strftime.c libc/time/strptime.c libc/time/osv-time.h libc/locale/rpmatch.c libc/locale/strfmon.c; do
  [ -f $T/$f ] && cp $T/$f src/; done

S="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"
F="$S -Wall -Wno-parentheses -Ishim -include shim/pre.h"
gcc $F -c -o o/strftime.o src/strftime.c
gcc $F -c -o o/shim.o shim.c
gcc $S -c -o o/sfdiff.o sfdiff.c
gcc $S -o sfd o/sfdiff.o o/strftime.o o/shim.o
gcc $F -c -o o/strptime.o src/strptime.c
gcc $S -c -o o/spdiff.o spdiff.c
gcc $S -o spd o/spdiff.o o/strptime.o o/strftime.o o/shim.o
gcc $F -c -o o/strfmon.o src/strfmon.c
gcc $S -c -o o/fmdiff.o fmdiff.c
gcc $S -o fmd o/fmdiff.o o/strfmon.o o/shim.o -lm
gcc $F -c -o o/rpmatch.o src/rpmatch.c
# The fuzz harness supplies the shim's locale functions itself (no shim.o).
# Since Stage 1c the merged harness also drives LC_COLLATE (osv_strcoll etc.),
# so it links collate.cc built with -DOSV_COLLATE_HOST (which compiles out the
# OSv libc entry points and leaves the osv_ host functions the harness calls).
X="-std=gnu++17 $S -I$T/libc/locale"
g++ $X -c -o o/locale-data.o $T/libc/locale/locale-data.cc
g++ $X -DOSV_COLLATE_HOST -c -o o/collate.o $T/libc/locale/collate.cc
g++ $X -c -o o/fuzz.o ../fuzz-locale-data.cc
g++ $S -o fuzz-locale-data o/fuzz.o o/locale-data.o o/collate.o o/strftime.o o/strptime.o o/strfmon.o o/rpmatch.o -lm
nm o/sfdiff.o | grep -q ' U strftime_l$' && nm o/sfdiff.o | grep -q ' U osv___strftime_l$'
