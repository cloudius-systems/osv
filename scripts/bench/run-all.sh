#!/bin/bash
# Phase E driver -- runs INSIDE container bx. Loops workloads, one qemu at a
# time, each timeout-wrapped + pkill fallback, logs to files, parses RESULT
# into results.tsv, banks nothing (host-side cp done by caller after each).
# Copyright (C) 2026 Greg Burd
#
# Usage (inside container): run-all.sh <phase>
#   phase: sv-core | sv-rest | raidz
# Env: SVDISK=/dev/vblk0 backing NVMe passed as first virtio-blk.
set -u
OUT=/w/bench-out
mkdir -p "$OUT/logs"
TSV="$OUT/results.tsv"
[ -f "$TSV" ] || printf 'impl\ttopo\tworkload\tmetric\tvalue\n' > "$TSV"

OZFS=/w/osv-ozfs/build/release.x64/zfs_builder.elf
BSD=/w/osv-bsd/build/release.x64/zfs_builder.elf
SVDISK=/dev/nvme1n1
# raidz: 7 preallocated files (created by caller) bind-mounted at /raidz
RZDIR=/raidz
RZ=("$RZDIR/f0" "$RZDIR/f1" "$RZDIR/f2" "$RZDIR/f3" "$RZDIR/f4" "$RZDIR/f5" "$RZDIR/f6")

# run1 <impl> <topo> <tag> <workload> <kv...>
run1() {
    local impl="$1" topo="$2" tag="$3" wl="$4"; shift 4
    local kv=("$@")
    local elf; [ "$impl" = openzfs ] && elf=$OZFS || elf=$BSD
    local log="$OUT/logs/${impl}_${topo}_${tag}.log"

    # ANTI-WEDGE: kill any qemu, settle, before launch. ONE at a time.
    pkill -9 -f qemu-system-x86 2>/dev/null; sleep 2

    # build virtio-blk drive list + vdevs arg
    local drives vdevs
    if [ "$topo" = single ]; then
        wipefs -a "$SVDISK" >/dev/null 2>&1
        drives=(-device virtio-blk-pci,id=blk0,drive=hd0 \
                -drive "file=$SVDISK,if=none,id=hd0,format=raw,cache=none,aio=threads")
        vdevs="vdevs=/dev/vblk0"
    else
        drives=(); local i=0 dl=""
        for f in "${RZ[@]}"; do
            drives+=(-device "virtio-blk-pci,id=blk$i,drive=hd$i" \
                     -drive "file=$f,if=none,id=hd$i,format=raw,cache=none,aio=threads")
            [ -n "$dl" ] && dl="$dl,"; dl="$dl/dev/vblk$i"; i=$((i+1))
        done
        vdevs="vdevs=raidz2:$dl"
    fi

    echo ">>> $impl $topo $tag $wl ${kv[*]}"
    timeout 300 qemu-system-x86_64 -m 8G -smp 4 -enable-kvm -cpu host \
        -nographic -no-reboot -kernel "$elf" \
        "${drives[@]}" \
        -append "--nomount --noinit --preload-zfs-library /zfs-bench.so $wl $vdevs impl=$impl ${kv[*]}" \
        > "$log" 2>&1
    local rc=$?
    pkill -9 -f qemu-system-x86 2>/dev/null; sleep 1
    echo "    rc=$rc"

    # parse RESULT lines (strings: log may contain terminal control bytes)
    strings "$log" | grep '^RESULT ' | while read -r _ name rest; do
        if echo "$rest" | grep -q 'median='; then
            local val sd
            val=$(echo "$rest" | sed -n 's/.*median=\([0-9.]*\).*/\1/p')
            sd=$(echo "$rest" | sed -n 's/.*stdev=\([0-9.]*\).*/\1/p')
            printf '%s\t%s\t%s\t%s\t%s (sd %s)\n' "$impl" "$topo" "$wl" "$name" "$val" "$sd" >> "$TSV"
        else
            local val
            val=$(echo "$rest" | awk '{print $1}' | tr -d '~')
            printf '%s\t%s\t%s\t%s\t%s\n' "$impl" "$topo" "$wl" "$name" "$val" >> "$TSV"
        fi
    done
    strings "$log" | grep -E '^RESULT ' | sed 's/^/    /'
}

PHASE="${1:-sv-core}"

case "$PHASE" in
sv-core)
    # Diagnostic core, single-vdev, 8G guest RAM (ARC ~4-6G).
    # CALIBRATED: seqwrite per-rep <= 2G (3 x 4G stalls the OpenZFS txg path at
    # 8G RAM). Reads run at 6G fine. mmap at 512M/1.5G (WS<ARC) works; a 6G mmap
    # would need a 6G write first which stalls, so the ARC-bridge measure uses
    # the WS<ARC point (freedelta shows the cache footprint either way).
    for impl in openzfs bsd; do
        run1 $impl single seqwrite     seqwrite     size_mb=2048 recsize=1M reps=3
        run1 $impl single seqread_cold seqread_cold size_mb=6144 recsize=1M reps=3
        run1 $impl single seqread_warm seqread_warm size_mb=3072 recsize=1M reps=3
        run1 $impl single mmap_under   mmapread     size_mb=512  recsize=128k reps=3
        run1 $impl single mmap_near    mmapread     size_mb=1536 recsize=128k reps=3
    done
    # O_DIRECT: OpenZFS only (BSD-ZFS has no direct=). Compare vs cached.
    run1 openzfs single odirect odirect size_mb=2048 recsize=1M reps=1 direct=1
    ;;
sv-rest)
    for impl in openzfs bsd; do
        run1 $impl single randread  randread  size_mb=4096 bs=4096 secs=30 reps=3
        run1 $impl single randwrite randwrite size_mb=2048 bs=4096 qd=8 secs=20 reps=3
        run1 $impl single fsync     fsync     secs=15 reps=3
        run1 $impl single meta      meta      nfiles=50000 reps=1
        run1 $impl single scrub     scrub     size_mb=2048 recsize=1M reps=1
    done
    run1 openzfs single compress_lz4 compress comp=lz4 size_mb=2048 recsize=1M
    run1 openzfs single compress_off compress comp=off size_mb=2048 recsize=1M
    run1 bsd     single compress_lz4 compress comp=lz4 size_mb=2048 recsize=1M
    run1 bsd     single compress_off compress comp=off size_mb=2048 recsize=1M
    ;;
raidz)
    for impl in openzfs bsd; do
        run1 $impl raidz seqwrite     seqwrite     size_mb=2048 recsize=1M reps=3
        run1 $impl raidz seqread_cold seqread_cold size_mb=6144 recsize=1M reps=3
        run1 $impl raidz seqread_warm seqread_warm size_mb=3072 recsize=1M reps=3
        run1 $impl raidz mmap_under   mmapread     size_mb=512  recsize=128k reps=3
        run1 $impl raidz randread     randread     size_mb=4096 bs=4096 secs=30 reps=3
    done
    ;;
esac
echo "=== phase $PHASE done ==="
