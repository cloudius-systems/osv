#!/bin/bash
# Phase E ZFS microbench driver. Boots zfs_builder.elf under QEMU/KVM with
# raw NVMe (or preallocated raidz files) as virtio-blk disks and runs one
# workload of /zfs-bench.so, capturing serial output.
# Copyright (C) 2026 Greg Burd
#
# Usage: run-bench.sh <elf> <outfile> <workload> [k=v ...] -- [extra virtio-blk files]
#   elf       path to zfs_builder.elf
#   outfile   where to tee serial console
#   workload  seqwrite|seqread_cold|...|info
#   disks after '--' are attached as vblk1,vblk2,... (vblk0 is the boot image? no:
#            zfs_builder.elf is booted as -kernel; the FIRST -drive becomes vblk0
#            which the harness ignores; we start data disks at vblk0). To keep the
#            harness default (/dev/vblk1) we always attach one throwaway image as
#            vblk0 then the real disks from vblk1.
set -u
ELF="$1"; OUT="$2"; WL="$3"; shift 3
KV=(); DISKS=()
mode=kv
for a in "$@"; do
    if [ "$a" = "--" ]; then mode=disk; continue; fi
    if [ "$mode" = kv ]; then KV+=("$a"); else DISKS+=("$a"); fi
done

MEM="${MEM:-8G}"
CPUS="${CPUS:-4}"
# Disk cache mode. OSv's own zfs image build uses writeback; cache=none+aio=native
# on a raw block dev stalled the OpenZFS txg-sync path, so default to writeback.
DISKOPT="${DISKOPT:-format=raw,cache=writeback,aio=threads}"

# vblk0: tiny throwaway (harness never touches it); real data disks -> vblk1+
THROW=$(mktemp /tmp/vblk0.XXXX.img)
qemu-img create -f raw "$THROW" 64M >/dev/null 2>&1

DRIVES=(-device virtio-blk-pci,id=blk0,drive=hd0 \
        -drive "file=$THROW,if=none,id=hd0,$DISKOPT")
idx=1
for d in "${DISKS[@]}"; do
    DRIVES+=(-device "virtio-blk-pci,id=blk$idx,drive=hd$idx" \
             -drive "file=$d,if=none,id=hd$idx,$DISKOPT")
    idx=$((idx+1))
done

APPEND="--norandom --nomount --noinit --preload-zfs-library /zfs-bench.so $WL ${KV[*]}"

TIMEOUT="${TIMEOUT:-300}"
echo "== booting: WL=$WL KV=${KV[*]} disks=${DISKS[*]} mem=$MEM ==" | tee "$OUT"
timeout "$TIMEOUT" qemu-system-x86_64 \
    -kernel "$ELF" -m "$MEM" -smp "$CPUS" \
    -cpu host -enable-kvm -nographic -no-reboot \
    "${DRIVES[@]}" \
    -append "$APPEND" 2>&1 | tee -a "$OUT"
rc=${PIPESTATUS[0]}
rm -f "$THROW"
echo "== qemu exit rc=$rc ==" | tee -a "$OUT"
