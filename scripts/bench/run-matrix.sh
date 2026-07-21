#!/bin/bash
# Phase E full matrix runner. Runs the workload set on both impls and topologies,
# banking each raw serial log + a parsed results.tsv. Run INSIDE the container.
# Copyright (C) 2026 Greg Burd
#
# cwd is irrelevant; uses absolute /w paths. Results -> /w/bench-out/.
set -u
# ANTI-WEDGE: cap guest RAM at 8G (NOT 16G). Concurrent qemu + big backing
# files thrash the host at 16G; 8G is plenty for ARC (>=4G). Working sets stay
# bounded (<= 6G) and always < guest RAM to avoid the OpenZFS >RAM txg stall.
export MEM=8G
export TIMEOUT=300
OUT=/w/bench-out
mkdir -p "$OUT/logs"
TSV="$OUT/results.tsv"
[ -f "$TSV" ] || echo -e "impl\ttopo\tworkload\tmetric\tvalue" > "$TSV"

OZFS=/w/osv-ozfs/build/release.x64/zfs_builder.elf
BSD=/w/osv-bsd/build/release.x64/zfs_builder.elf
RUN_OZFS=/w/osv-ozfs/scripts/bench/run-bench.sh
RUN_BSD=/w/osv-bsd/scripts/bench/run-bench.sh

# single-vdev raw NVMe (whole disk) and raidz2 over 7 files.
SV_DISK=/dev/nvme1n1
RZ_FILES=(/w/raidz/f0 /w/raidz/f1 /w/raidz/f2 /w/raidz/f3 /w/raidz/f4 /w/raidz/f5 /w/raidz/f6)

# one run: impl topo workload kv...
# emits RESULT lines into a log and appends parsed metrics to TSV.
run1() {
    local impl="$1" topo="$2" wl="$3"; shift 3
    local kv=("$@")
    local elf run vdevs disks
    if [ "$impl" = openzfs ]; then elf=$OZFS; run=$RUN_OZFS; else elf=$BSD; run=$RUN_BSD; fi
    if [ "$topo" = single ]; then
        vdevs="vdevs=/dev/vblk1"; disks=("$SV_DISK")
    else
        vdevs="vdevs=raidz2:/dev/vblk1,/dev/vblk2,/dev/vblk3,/dev/vblk4,/dev/vblk5,/dev/vblk6,/dev/vblk7"
        disks=("${RZ_FILES[@]}")
    fi
    local log="$OUT/logs/${impl}_${topo}_${wl}.log"
    # disambiguate repeated workloads (e.g. two mmapread sizes) by size_mb
    for a in "${kv[@]}"; do case "$a" in size_mb=*) log="$OUT/logs/${impl}_${topo}_${wl}_${a#size_mb=}.log";; esac; done
    echo ">>> $impl $topo $wl ${kv[*]}"
    # clear any leftover qemu holding a disk write-lock
    pkill -9 -f qemu-system 2>/dev/null; sleep 1
    bash "$run" "$elf" "$log" "$wl" "$vdevs" impl="$impl" "${kv[@]}" -- "${disks[@]}" >/dev/null 2>&1
    # parse RESULT lines
    grep '^RESULT ' "$log" | while read -r _ name rest; do
        # forms: "RESULT foo median=X stdev=Y unit ..." or "RESULT foo VAL unit"
        if echo "$rest" | grep -q 'median='; then
            val=$(echo "$rest" | sed -n 's/.*median=\([0-9.]*\).*/\1/p')
            sd=$(echo "$rest" | sed -n 's/.*stdev=\([0-9.]*\).*/\1/p')
            echo -e "$impl\t$topo\t$wl\t$name\t${val} (sd ${sd})" >> "$TSV"
        else
            val=$(echo "$rest" | awk '{print $1}' | tr -d '~')
            echo -e "$impl\t$topo\t$wl\t$name\t$val" >> "$TSV"
        fi
    done
    tail -3 "$log" | sed 's/^/    /'
}

PHASE="${1:-core}"

if [ "$PHASE" = core ]; then
    # Diagnostic core on single-vdev at 8G guest RAM (ARC ~4-6G). Seq WS=6G
    # (> ARC for a cold/vdev-bound read, still < 8G RAM so no >RAM txg stall).
    # mmap: WS<ARC (1.5G) and WS slightly>ARC (6G) -> the ARC-bridge measure.
    for impl in openzfs bsd; do
        run1 $impl single seqwrite      size_mb=6144  recsize=1M reps=3
        run1 $impl single seqread_cold  size_mb=6144  recsize=1M reps=3
        run1 $impl single seqread_warm  size_mb=3072  recsize=1M reps=3
        run1 $impl single mmapread      size_mb=1536  recsize=128k reps=3
        run1 $impl single mmapread      size_mb=6144  recsize=128k reps=3
    done
    # O_DIRECT: OpenZFS only. direct=1 => zfs set direct=always, O_DIRECT open.
    run1 openzfs single odirect  size_mb=4096 recsize=1M reps=1 direct=1
    # cached seqwrite on OpenZFS + BSD for the O_DIRECT comparison table
    run1 openzfs single seqwrite size_mb=4096 recsize=1M reps=1 direct=0
    run1 bsd     single seqwrite size_mb=4096 recsize=1M reps=1 direct=0
elif [ "$PHASE" = rest ]; then
    for impl in openzfs bsd; do
        run1 $impl single randread  size_mb=4096 bs=4096 secs=30 reps=3
        run1 $impl single randwrite size_mb=4096 bs=4096 qd=8 secs=30 reps=3
        run1 $impl single fsync     secs=20 reps=3
        run1 $impl single meta      nfiles=100000 reps=1
        run1 $impl single scrub     size_mb=4096 recsize=1M reps=1
    done
    # compression: lz4 on vs off (both impls)
    run1 openzfs single compress comp=lz4 size_mb=4096 recsize=1M
    run1 openzfs single compress comp=off size_mb=4096 recsize=1M
    run1 bsd     single compress comp=lz4 size_mb=4096 recsize=1M
    run1 bsd     single compress comp=off size_mb=4096 recsize=1M
elif [ "$PHASE" = raidz ]; then
    for impl in openzfs bsd; do
        run1 $impl raidz seqwrite     size_mb=4096  recsize=1M reps=3
        run1 $impl raidz seqread_cold size_mb=4096  recsize=1M reps=3
        run1 $impl raidz seqread_warm size_mb=4096  recsize=1M reps=3
        run1 $impl raidz mmapread     size_mb=512  recsize=128k reps=3
        run1 $impl raidz randread     size_mb=4096 bs=4096 secs=30 reps=3
    done
fi
echo "=== phase $PHASE done ==="
