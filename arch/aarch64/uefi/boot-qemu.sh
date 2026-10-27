#!/usr/bin/env bash
# boot-qemu.sh - boot a GPT/ESP disk image under qemu-system-aarch64 with AAVMF
# (UEFI firmware), capture the serial console to a file, and time out.
#
# Usage: ./boot-qemu.sh <disk.img> [console-out.txt] [timeout-secs] [mem-mib]
set -euo pipefail

IMG="${1:?disk image}"
CONS="${2:-/tmp/osv-uefi-console.txt}"
TMO="${3:-30}"
MEM="${4:-2048}"
ACPI="${5:-off}"   # OSv/aarch64 is DTB-only; acpi=off makes AAVMF publish a DTB

# Writable copy of the UEFI vars (AAVMF_VARS is a template).
VARS="$(mktemp --suffix=.vars.fd)"
# AAVMF pflash must be exactly 64 MiB; AAVMF_VARS template is smaller -> pad.
cp /usr/share/edk2/aarch64/vars-template-pflash.raw "$VARS"
CODE=/usr/share/edk2/aarch64/QEMU_EFI-pflash.raw   # verbose edk2 build

: > "$CONS"
echo "booting $IMG under AAVMF (timeout ${TMO}s, mem ${MEM}M)"
set +e
timeout "$TMO" qemu-system-aarch64 \
  -machine virt,gic-version=3,acpi=$ACPI \
  -cpu max -smp 1 -m "$MEM" \
  -drive if=pflash,format=raw,readonly=on,file="$CODE" \
  -drive if=pflash,format=raw,file="$VARS" \
  -drive if=none,format=raw,file="$IMG",id=hd0 \
  -device virtio-blk-device,drive=hd0 \
  -display none -serial "file:$CONS" -monitor none </dev/null \
  -no-reboot >/dev/null 2>&1
rc=$?
set -e
rm -f "$VARS"
echo "qemu exit=$rc; console bytes: $(stat -c %s "$CONS")"
echo "===== console ====="
cat "$CONS"
echo "===== /console ====="
