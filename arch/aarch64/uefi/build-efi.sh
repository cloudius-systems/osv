#!/usr/bin/env bash
# build-efi.sh - build the OSv UEFI stub as a PE/COFF .efi (gnu-efi).
#
# Usage: ./build-efi.sh <arch> [stub|full]
#   arch: aarch64 | x86_64
#   mode: stub (STUB_ONLY, prints+halts) | full (loads+enters OSv). default: stub
#
# Produces: out/BOOTAA64.EFI (aarch64) or out/BOOTX64.EFI (x86_64).
set -euo pipefail

ARCH="${1:-aarch64}"
MODE="${2:-stub}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/out"
mkdir -p "$OUT"

case "$ARCH" in
  aarch64)
    GNUEFI_ARCH=aarch64
    EFINAME=BOOTAA64.EFI
    ARCHDEF=-DOSVEFI_ARCH_AARCH64
    # gnu-efi ships these in /usr/lib on Fedora aarch64
    CRT0=/usr/lib/crt0-efi-aarch64.o
    LDS=/usr/lib/elf_aarch64_efi.lds
    # PE machine type for aarch64 (IMAGE_FILE_MACHINE_ARM64 = 0xaa64)
    OBJCOPY_TARGET=efi-app-aarch64
    ;;
  x86_64)
    GNUEFI_ARCH=x86_64
    EFINAME=BOOTX64.EFI
    ARCHDEF=-DOSVEFI_ARCH_X86_64
    CRT0=/usr/lib/crt0-efi-x86_64.o
    LDS=/usr/lib/elf_x86_64_efi.lds
    OBJCOPY_TARGET=efi-app-x86_64
    ;;
  *) echo "unknown arch $ARCH" >&2; exit 1 ;;
esac

STUBDEF=""
SRCS="$HERE/osvefi.c"
if [ "$MODE" = stub ]; then
  STUBDEF=-DSTUB_ONLY
else
  SRCS="$SRCS $HERE/osvefi_boot.c"
fi

INCL="-I/usr/include/efi -I/usr/include/efi/$GNUEFI_ARCH -I/usr/include/efi/protocol"
# gnu-efi app build flags (freestanding, PIC, no stdlib).
CFLAGS="-fpic -fshort-wchar -fno-stack-protector -fno-strict-aliasing -ffreestanding \
  -fno-merge-all-constants -Wall -Wextra -mstrict-align $ARCHDEF $STUBDEF $INCL"
if [ "$ARCH" = aarch64 ]; then
  CFLAGS="$CFLAGS -mgeneral-regs-only"
fi

OBJS=""
for s in $SRCS; do
  o="$OUT/$(basename "${s%.c}").o"
  echo "CC  $s"
  gcc $CFLAGS -c "$s" -o "$o"
  OBJS="$OBJS $o"
done

echo "LD  $OUT/osvefi.so"
# Link a relocatable shared object per gnu-efi convention, then objcopy to PE.
ld -nostdlib -znocombreloc -shared -Bsymbolic \
   -T "$LDS" "$CRT0" $OBJS \
   -o "$OUT/osvefi.so" \
   -L/usr/lib -lgnuefi -lefi

# Verify no unresolved dynamic deps crept in.
if readelf -d "$OUT/osvefi.so" 2>/dev/null | grep -q NEEDED; then
  echo "ERROR: .so has NEEDED deps (not freestanding)" >&2
  readelf -d "$OUT/osvefi.so" | grep NEEDED >&2
  exit 1
fi

echo "OBJCOPY $OUT/$EFINAME"
objcopy -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym \
  -j .rel -j .rela -j .rel.* -j .rela.* -j .reloc \
  --target "$OBJCOPY_TARGET" \
  "$OUT/osvefi.so" "$OUT/$EFINAME"

echo "built $OUT/$EFINAME:"
ls -l "$OUT/$EFINAME"
file "$OUT/$EFINAME" || true
# Confirm PE/COFF magic 'MZ'
head -c2 "$OUT/$EFINAME" | od -An -c | tr -d ' \n'; echo " (expect MZ)"
