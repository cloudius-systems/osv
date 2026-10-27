#!/usr/bin/env bash
# build-testkern.sh - build the contract-faithful ARM64 Image test payload.
# Produces out/testkern.bin, a flat ARM64 Image (b prestart / magic at 0x38 /
# image_size at 0x10) that the UEFI stub can load and enter exactly like OSv's
# real loader.img.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/out"; mkdir -p "$OUT"

gcc -c -ffreestanding -mgeneral-regs-only "$HERE/testkern.S" -o "$OUT/testkern.o"
# Link flat at address 0 (position-independent via adr), text then rodata.
ld -Ttext=0 --oformat=elf64-littleaarch64 "$OUT/testkern.o" -o "$OUT/testkern.elf"
objcopy -O binary "$OUT/testkern.elf" "$OUT/testkern.bin"

# Patch image_size (LE64 at offset 0x10) to the actual file size (round to 2 MiB).
SZ=$(stat -c %s "$OUT/testkern.bin")
# round up to 2 MiB
ALIGN=$((2*1024*1024))
IMGSZ=$(( (SZ + ALIGN - 1) / ALIGN * ALIGN ))
python3 - "$OUT/testkern.bin" "$IMGSZ" <<'PY'
import sys, struct
f, sz = sys.argv[1], int(sys.argv[2])
b = bytearray(open(f,'rb').read())
struct.pack_into('<Q', b, 0x10, sz)   # image_size at 0x10
open(f,'wb').write(b)
print(f"testkern.bin {len(b)} bytes, image_size field=0x{sz:x}")
PY
echo "magic @0x38:"; xxd -s 0x38 -l 4 "$OUT/testkern.bin"
echo "built $OUT/testkern.bin"; ls -l "$OUT/testkern.bin"
