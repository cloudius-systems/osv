# OSv UEFI boot stub (aarch64)

OSv has no UEFI boot support today: x86 boots via the legacy BIOS/MBR path, and
elsewhere the kernel is loaded as an ELF or a Linux ARM64 `Image` by the VMM
(QEMU `-kernel`, Firecracker). There is no PE/COFF `.efi` entry and no EFI System
Partition anywhere in the tree.

This directory adds a UEFI boot stub: a PE/COFF `.efi` application that the
firmware loads, which then loads the OSv aarch64 kernel from an EFI System
Partition, acquires a device tree, calls `GetMemoryMap` + `ExitBootServices`, and
enters OSv at its existing aarch64 entry contract. No OSv-side change is needed:
the stub reproduces exactly the handoff that QEMU `-kernel` and Firecracker
already perform, so a normally built `loader.img` is a drop-in payload.

This is an additive, standalone foundation. It is deliberately **not** wired into
the default build (`Makefile` / `scripts/build`) yet; see "What remains" below.

## The aarch64 entry contract this reproduces

OSv/aarch64 is launched today as a Linux ARM64 `Image`. `loader.img` is
`preboot.bin` (offset 0) followed by `loader-stripped.elf` (offset `0x10000`).
`preboot.bin`'s `_head` (`arch/aarch64/preboot.S`) is a Linux ARM64 Image header:

- `0x00`: `b prestart`
- `0x08`: `text_offset` = `0x80000`
- `0x10`: `image_size` (written by `imgedit.py setsize_aarch64`)
- `0x38`: magic `"ARM\x64"` (`0x644d5241`)

QEMU `-kernel` and Firecracker use the Linux ARM64 boot protocol: load the Image
at a 2 MiB-aligned base + `text_offset`, build and place a DTB, then enter at the
Image base with `x0` = DTB physical address, at EL1, MMU off, caches off,
interrupts masked. `prestart` sets `x5` = DTB (from `x0`), `x4` = base + `0x200`
(cmdline), `x3` = base + `0x10000` (ELF header), and branches into `start_elf`
(`arch/aarch64/boot.S`), which requires EL1 and consumes `x3`/`x4`/`x5`.

So OSv's aarch64 entry already takes `x0` = DTB the Linux-Image way: there is no
mismatch and no OSv-side entry shim is required. The stub is simply the loader
that QEMU `-kernel` / Firecracker already are.

## Files

- `osvefi.c` - arch-neutral EFI entry and ConOut console. The `STUB_ONLY` build
  is the Stage-1 variant: print to ConOut, enumerate the config tables, report
  DTB presence, and halt.
- `osvefi_boot.c` - load the kernel from the ESP, place the cmdline at
  base + `0x200`, acquire the DTB, `GetMemoryMap`, `ExitBootServices`, and the
  arch-specific jump (`osv_enter_kernel`). Only `osv_enter_kernel` is
  arch-specific; everything else is arch-neutral gnu-efi.
- `testkern.S` - a contract-faithful ARM64-Image test payload. It mirrors
  `_head` byte for byte and repeats `prestart`'s register handoff, then reads
  `x0`/`x5` and prints its exception level and the DTB magic over PL011. A
  successful boot of this payload by the stub proves the handoff is honoured.
- `build-efi.sh` - gnu-efi build producing `BOOTAA64.EFI` (or `BOOTX64.EFI`).
- `build-esp.sh` - GPT + ESP (FAT) disk-image builder (`sgdisk` + mtools; no
  root or loop mounts). This is the UEFI-image counterpart to `imgedit.py`,
  which stays MBR-only for the legacy x86 path.
- `build-testkern.sh` - assemble the test payload and patch its `image_size`.
- `boot-qemu.sh` - boot a GPT/ESP image under `qemu-system-aarch64` with AAVMF
  (UEFI firmware), capturing the serial console.

## Design

1. `efi_main` runs `InitializeLib` and prints a banner to ConOut.
2. Open the ESP this `.efi` was loaded from (LoadedImage -> SimpleFileSystem ->
   OpenVolume) and read `\EFI\BOOT\osv.bin` (the OSv `loader.img`) into
   2 MiB-aligned `EfiLoaderData` pages.
3. Validate the ARM64 Image magic at offset `0x38`.
4. Read `\EFI\BOOT\cmdline` (or use a default) and write it at base + `0x200`.
5. Acquire a DTB from the FDT configuration table
   (`b1b621d5-f19c-41a5-830b-d9152c69aae0`). If absent (ACPI-only firmware),
   halt with a precise diagnostic (see "What remains").
6. `GetMemoryMap` -> `ExitBootServices` (re-fetch the map key once if it goes
   stale, the standard UEFI race).
7. Arch handoff: on aarch64 set `x0` = DTB and branch to the Image base.

Steps 1 to 6 are arch-neutral and compile identically for both arches (one
`#ifdef` selects the `.efi` name and PE machine type). The only arch-specific
code is `osv_enter_kernel`:

- aarch64: set `x0` = DTB and `br` to the ARM64 Image base (implemented).
- x86_64: from a UEFI x86 app the CPU is already in 64-bit long mode with paging
  on, so the handoff needs a small trampoline reproducing the state OSv's
  32/64-bit loader entry expects (x86 reads ACPI natively, so no DTB).
  Documented and scaffolded; not wired here.

## What is proven, and how

Under qemu-AAVMF (UEFI firmware), driven by `boot-qemu.sh`:

- **Stage 1**: the `STUB_ONLY` `BOOTAA64.EFI`, booted off a GPT/ESP disk, prints
  to the UEFI console, enumerates the configuration tables (the default build
  publishes an ACPI 2.0 RSDP and no FDT; with `acpi=off` AAVMF instead publishes
  the FDT table with magic `d00dfeed`), and halts. This proves the toolchain,
  the PE/COFF `.efi`, the GPT/ESP plumbing, and the firmware entry end to end,
  and establishes the ACPI-vs-DTB firmware behaviour.
- **Stage 2**: the full `BOOTAA64.EFI` (with `acpi=off`, so AAVMF hands a DTB)
  loads `testkern.bin` from the ESP, validates the Image magic, places the
  cmdline, acquires the DTB, runs `ExitBootServices`, and jumps. The payload
  then prints a PL011 banner reporting `EL=1` and an intact DTB magic
  (`d00dfeed`), which is exactly the state OSv's `start_elf` requires.

Because the payload reproduces OSv's entry contract exactly, a real OSv
`loader.img` dropped in as `\EFI\BOOT\osv.bin` is compatible with the stub's
handoff by construction.

### Reproducing (aarch64 host with gnu-efi, qemu, AAVMF, mtools, gdisk)

```
./build-efi.sh aarch64 full
./build-testkern.sh
./build-esp.sh aarch64 out/BOOTAA64.EFI out/testkern.bin out/osv-uefi-aarch64.img
./boot-qemu.sh out/osv-uefi-aarch64.img /tmp/console.txt 20 1024 off
```

## What remains (scope)

This is a proven foundation, not a complete native-boot path. Three items are
outstanding, in rough priority order:

1. **ACPI -> DTB synthesis in the stub.** Real server-class Arm firmware (SBBR/
   SBSA, as on cloud Arm instances) is ACPI-only and hands no DTB, while OSv is
   DTB-only (no ACPI parser on aarch64). On such firmware the stub reaches
   ConOut and then stops at the "no DTB" gate. Native boot on ACPI-only Arm
   hardware needs the stub to synthesise a DTB before `ExitBootServices` from
   the UEFI memory map plus the MADT/GTDT/SPCR/MCFG/FADT ACPI tables, and pass
   it in `x0`. This is a bounded translation (roughly 400 to 700 lines of C) and
   `libfdt` is already in the tree (`external/aarch64/libfdt`, used by
   `arch/aarch64/arch-dtb.cc`). It keeps all the complexity in the stub, out of
   the kernel. The mapping the kernel needs:
   `/memory` from GetMemoryMap; `/cpus` + MPIDs and the GIC nodes from MADT;
   `/timer` from GTDT; the UART from SPCR; PCI ECAM from MCFG plus interrupt
   routing; and the PSCI method from the FADT Arm boot flags.
2. **Wire the stub into the default build.** Add the `.efi` + ESP image rules to
   `Makefile` / `scripts/build` as an optional UEFI image target. Deliberately
   deferred until (1), so this change adds the stub and its standalone build and
   test scripts without touching the default build.
3. **x86_64 `BOOTX64.EFI`.** The arch-neutral core compiles for x86; only
   `osv_enter_kernel` is arch-specific (the long-mode trampoline into OSv's
   loader entry). Documented, not yet built.
