#!/bin/sh
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# Build glibc's localedef from the pinned source tree, for a build host whose
# own localedef is older than 2.41 (the host's is byte-identical to 2.44 for
# this data when it is >= 2.41, verified in Stage 2, so this path is a
# fallback). Prints to stdout a command that runs the built localedef.
#   build-localedef.sh <glibc-source-tree>
# The source tree must be the full extracted glibc-<ver> (configure needs more
# than localedata/); when this path is taken the Makefile extracts it.
set -eu

SRC=$1
BUILD=$SRC/../build-localedef
mkdir -p "$BUILD"
cd "$BUILD"

if [ ! -x locale/localedef ]; then
    "$SRC/configure" --prefix="$BUILD/inst" --disable-werror --disable-profile \
        >configure.log 2>&1
    make -j"$(nproc)" -r locale/others >build.log 2>&1
fi

LD="$BUILD/elf/ld.so"
LIBPATH="$BUILD:$BUILD/locale"
# The built localedef is linked against the freshly built libc; run it through
# the matching loader.
echo "$LD --library-path $LIBPATH $BUILD/locale/localedef"
