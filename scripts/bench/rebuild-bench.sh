#!/bin/bash
# Rebuild the zfs-bench.so harness into an existing OSv build tree and
# regenerate zfs_builder.elf. Run INSIDE the fedora build container, cwd = clone root.
# Copyright (C) 2026 Greg Burd
set -e
CONF="${1:-openzfs}"     # openzfs | bsd
CLONE="$(pwd)"
BUILD="$CLONE/build/release.x64"

# PURE C .so: gcc (NOT g++) so no libstdc++ symbols (std::string@GLIBCXX,
# std::chrono, operator new/delete, __cxa_*) which OSv can't resolve at load.
gcc -std=gnu11 -g -O2 -fPIC -shared -D__OSV__ -D_GNU_SOURCE \
    -o "$BUILD/zfs-bench.so" "$CLONE/scripts/bench/zfs-bench.c" -lm

# HARD GATE: any GLIBCXX/CXXABI/__isoc23 symbol = the .so won't load in OSv.
if nm -D "$BUILD/zfs-bench.so" | grep -Eq 'GLIBCXX|CXXABI|__isoc23'; then
    echo "FATAL: zfs-bench.so pulls unresolvable symbols:" >&2
    nm -D "$BUILD/zfs-bench.so" | grep -E 'GLIBCXX|CXXABI|__isoc23' >&2
    exit 1
fi
echo "OK: zfs-bench.so has no GLIBCXX/CXXABI/__isoc23 symbols"

# The Makefile regenerates zfs_builder_bootfs.manifest from the .skel on every
# build, so add zfs-bench.so to the SKEL (idempotent) to survive rebuilds.
SKEL="$CLONE/zfs_builder_bootfs.manifest.skel"
grep -q 'zfs-bench.so' "$SKEL" || \
    echo "/zfs-bench.so: ./zfs-bench.so" >> "$SKEL"

cd "$CLONE"
# Relink ONLY zfs_builder.elf (bootfs holds the bench .so). Do NOT run the full
# ./scripts/build -- its usr.img cpiod-populate step boots a guest that hangs
# intermittently and is not needed for benchmarking off the zfs_builder bootfs.
# MUST pass conf_zfs=$CONF: the manifest-gen $(shell) branches on it -- default
# (bsd) strips libzutil/libshare/libzfs_core/libtpool, which OpenZFS zpool.so
# needs (libzfs_core_init). Force bootfs regen so the new bench .so lands.
rm -f "$BUILD/zfs_builder_bootfs.bin" "$BUILD/zfs_builder_bootfs.o" "$BUILD/zfs_builder.elf"
make -C "$CLONE" conf_zfs="$CONF" OSV_NO_JAVA_TESTS=1 build/release.x64/zfs_builder.elf 2>&1 | tail -8
echo "REBUILT $CONF -> $BUILD/zfs_builder.elf"
