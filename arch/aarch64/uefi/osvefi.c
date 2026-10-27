/*
 * osvefi.c - a UEFI boot stub (PE/COFF .efi application) for OSv.
 *
 * Arch-shared boot-services logic; the kernel handoff is arch-specific and
 * selected by OSVEFI_ARCH_AARCH64 / OSVEFI_ARCH_X86_64 at build time.
 *
 * Stage 1 (STUB_ONLY): print to the UEFI console (ConOut) and halt - proves the
 * toolchain, PE/COFF, ESP/GPT plumbing and firmware entry end to end.
 *
 * Stage 2+ (full): load the OSv kernel image from the ESP (\EFI\BOOT\osv.bin),
 * obtain a DTB from the EFI config table, place the cmdline, GetMemoryMap +
 * ExitBootServices, then jump to the kernel with the arch handoff contract.
 *
 * This work is open source software, licensed under the terms of the BSD
 * license as described in the LICENSE file in the
 * top-level directory.
 */

#include <efi.h>
#include <efilib.h>

/* ---- arch handoff contract ------------------------------------------------
 *
 * aarch64: the OSv kernel is a Linux ARM64 Image (arch/aarch64/preboot.S _head).
 *   Enter at image_start (phys), MMU off, EL1, with x0 = DTB physical address.
 *   prestart decodes the Image and branches to start_elf (x3=elf,x4=cmdline,x5=dtb).
 *
 * x86_64: OSv uses the 32-bit boot protocol in arch/x64 (boot16/vmlinuz-boot).
 *   The UEFI x86 handoff differs (it sets up 64-bit long mode already) and needs
 *   a thin trampoline; documented in README.md, not yet wired here.
 */

/* Path of the OSv kernel payload on the ESP (same dir as the .efi). */
#define OSV_KERNEL_PATH  L"\\EFI\\BOOT\\osv.bin"

/* Offsets inside the OSv ARM64 Image (see arch/aarch64/preboot.S). */
#define OSV_CMDLINE_OFF  0x200    /* cmdline lives at image_start + 0x200 */
#define OSV_CMDLINE_MAX  0x1000   /* keep well under the 0x10000 ELF offset */

/* The device-tree table GUID published by EDK2/AAVMF on qemu -machine virt. */
#ifdef STUB_ONLY
static EFI_GUID FdtTableGuid = { 0xb1b621d5, 0xf19c, 0x41a5,
    { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 } };
#endif

static void print(CHAR16 *s)
{
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, s);
}

#ifndef STUB_ONLY
/* Stage 2+ entry, defined in osvefi_boot.c. */
EFI_STATUS osv_boot(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab);
#endif

/* Find a configuration table by GUID; returns its VendorTable pointer or NULL. */
#ifdef STUB_ONLY
static void *find_config_table(EFI_GUID *guid)
{
    for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
        if (CompareGuid(&ST->ConfigurationTable[i].VendorGuid, guid) == 0)
            return ST->ConfigurationTable[i].VendorTable;
    }
    return NULL;
}
#endif

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab)
{
    InitializeLib(image, systab);

    print(L"\r\nOSv EFI stub: hello from ConOut\r\n");

#ifdef STUB_ONLY
    /* Stage 1: prove we run, enumerate config tables, show DTB presence, halt. */
    Print(L"OSv EFI stub: %d config tables\r\n", (int)ST->NumberOfTableEntries);
    for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
        EFI_GUID *g = &ST->ConfigurationTable[i].VendorGuid;
        Print(L"  [%d] %08x-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x @ %lx\r\n",
              (int)i, g->Data1, g->Data2, g->Data3,
              g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
              g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7],
              (UINT64)ST->ConfigurationTable[i].VendorTable);
    }
    void *fdt = find_config_table(&FdtTableGuid);
    if (fdt) {
        Print(L"OSv EFI stub: DTB config table found at %lx\r\n", (UINT64)fdt);
        /* FDT magic is 0xd00dfeed big-endian = bytes de ed d0 0d? no: d0 0d fe ed */
        UINT8 *p = (UINT8 *)fdt;
        Print(L"OSv EFI stub: DTB first bytes %02x %02x %02x %02x\r\n",
              p[0], p[1], p[2], p[3]);
    } else {
        print(L"OSv EFI stub: no DTB config table (ACPI-only firmware?)\r\n");
    }
    print(L"OSv EFI stub: halting (stub-only build)\r\n");
    for (;;)
        ;
    return EFI_SUCCESS;
#else
    return osv_boot(image, systab);  /* Stage 2+ (osvefi_boot.c) */
#endif
}
