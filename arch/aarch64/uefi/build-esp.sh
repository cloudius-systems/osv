#!/usr/bin/env bash
# build-esp.sh - build a GPT-partitioned disk image with an EFI System Partition
# (FAT) holding \EFI\BOOT\<BOOTAA64|BOOTX64>.EFI and (optionally) the OSv kernel.
#
# Usage: ./build-esp.sh <arch> <efi-file> [kernel-payload] [out.img]
#   arch: aarch64 | x86_64   (selects BOOTAA64.EFI vs BOOTX64.EFI)
#   efi-file: the built .efi stub
#   kernel-payload: OSv kernel image to place at \EFI\BOOT\osv.bin (optional)
#   out.img: output disk image (default out/osv-uefi-<arch>.img)
#
# Uses sgdisk (GPT) + mkfs.fat + mmd/mcopy (mtools) - no root/loop mounts needed.
set -euo pipefail

ARCH="${1:?arch}"
EFI="${2:?efi file}"
KERNEL="${3:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${4:-$HERE/out/osv-uefi-$ARCH.img}"
mkdir -p "$(dirname "$OUT")"

case "$ARCH" in
  aarch64) BOOTNAME=BOOTAA64.EFI ;;
  x86_64)  BOOTNAME=BOOTX64.EFI ;;
  *) echo "unknown arch $ARCH" >&2; exit 1 ;;
esac

# --- size the ESP: 48 MiB header slack + efi + kernel, rounded up to MiB ---
EFI_SZ=$(stat -c %s "$EFI")
KRN_SZ=0
[ -n "$KERNEL" ] && KRN_SZ=$(stat -c %s "$KERNEL")
# FAT + payloads + generous slack; min 64 MiB so FAT32 is comfortable.
PAYLOAD_MIB=$(( (EFI_SZ + KRN_SZ) / 1048576 + 64 ))
ESP_MIB=$PAYLOAD_MIB
# Disk = 1 MiB GPT front + ESP + 1 MiB GPT backup slack.
DISK_MIB=$(( ESP_MIB + 2 ))

echo "ESP ${ESP_MIB} MiB, disk ${DISK_MIB} MiB (efi ${EFI_SZ} B, kernel ${KRN_SZ} B)"

# --- build the FAT ESP as a standalone filesystem image ---
FATIMG="$(mktemp --suffix=.fat)"
trap 'rm -f "$FATIMG"' EXIT
dd if=/dev/zero of="$FATIMG" bs=1M count="$ESP_MIB" status=none
mkfs.fat -F 32 -n ESP "$FATIMG" >/dev/null
mmd -i "$FATIMG" ::/EFI ::/EFI/BOOT
mcopy -i "$FATIMG" "$EFI" "::/EFI/BOOT/$BOOTNAME"
if [ -n "$KERNEL" ]; then
  mcopy -i "$FATIMG" "$KERNEL" "::/EFI/BOOT/osv.bin"
fi
echo "ESP contents:"
mdir -i "$FATIMG" -/ ::/ | sed 's/^/  /'

# --- build the GPT disk and embed the ESP at partition 1 ---
dd if=/dev/zero of="$OUT" bs=1M count="$DISK_MIB" status=none
# partition 1: start at 1 MiB (2048 sectors), size ESP_MIB, EF00 = ESP type.
sgdisk --clear \
  --new=1:2048:+${ESP_MIB}M --typecode=1:EF00 --change-name=1:"EFI System Partition" \
  "$OUT" >/dev/null
# write the FAT fs into partition 1 (seek 1 MiB = 2048 * 512).
dd if="$FATIMG" of="$OUT" bs=1M seek=1 conv=notrunc status=none

echo "built $OUT:"
ls -l "$OUT"
sgdisk -p "$OUT" | sed 's/^/  /'
