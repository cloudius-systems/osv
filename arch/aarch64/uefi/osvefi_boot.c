/*
 * osvefi_boot.c - Stage 2+ boot path for the OSv UEFI stub.
 *
 * Loads the OSv kernel image from the ESP, obtains a device tree, exits boot
 * services, and transfers control to the OSv kernel using the per-arch handoff
 * contract. The boot-services logic here is arch-neutral; only osv_enter_kernel()
 * is arch-specific.
 *
 * aarch64 handoff (arch/aarch64/preboot.S): the OSv kernel is a Linux ARM64
 *   Image. Enter at image base (phys), MMU off, EL1, x0 = DTB phys addr.
 *
 * This work is open source software, licensed under the terms of the BSD
 * license as described in the LICENSE file in the
 * top-level directory.
 */

#include <efi.h>
#include <efilib.h>

#define OSV_KERNEL_PATH   L"\\EFI\\BOOT\\osv.bin"
#define OSV_CMDLINE_OFF   0x200    /* cmdline at image_base + 0x200 (preboot.S) */
#define OSV_IMAGE_ALIGN   0x200000 /* ARM64 Image wants 2 MiB alignment */

/* Default cmdline if the ESP has no \EFI\BOOT\cmdline file. */
static CHAR8 default_cmdline[] = "--console=serial --verbose";

/* FDT (device tree) EFI configuration-table GUID. */
static EFI_GUID FdtTableGuid = { 0xb1b621d5, 0xf19c, 0x41a5,
    { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 } };
static EFI_GUID LoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID SimpleFsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

static void print(CHAR16 *s)
{
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, s);
}

static void *find_config_table(EFI_GUID *guid)
{
    for (UINTN i = 0; i < ST->NumberOfTableEntries; i++)
        if (CompareGuid(&ST->ConfigurationTable[i].VendorGuid, guid) == 0)
            return ST->ConfigurationTable[i].VendorTable;
    return NULL;
}

/* Read a file from the ESP (the volume this .efi was loaded from) into a
 * freshly allocated buffer. Returns EFI_SUCCESS and sets *buf/*size. */
static EFI_STATUS read_esp_file(EFI_HANDLE image, CHAR16 *path,
                                void **buf, UINTN *size)
{
    EFI_STATUS st;
    EFI_LOADED_IMAGE *li = NULL;
    EFI_FILE_IO_INTERFACE *fs = NULL;
    EFI_FILE *root = NULL, *f = NULL;

    st = uefi_call_wrapper(BS->HandleProtocol, 3, image, &LoadedImageGuid,
                           (void **)&li);
    if (EFI_ERROR(st)) return st;
    st = uefi_call_wrapper(BS->HandleProtocol, 3, li->DeviceHandle,
                           &SimpleFsGuid, (void **)&fs);
    if (EFI_ERROR(st)) return st;
    st = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
    if (EFI_ERROR(st)) return st;
    st = uefi_call_wrapper(root->Open, 5, root, &f, path,
                           EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st)) { uefi_call_wrapper(root->Close, 1, root); return st; }

    /* Query file size via EFI_FILE_INFO. */
    EFI_GUID fiGuid = EFI_FILE_INFO_ID;
    UINTN infosz = SIZE_OF_EFI_FILE_INFO + 256;
    EFI_FILE_INFO *info = AllocatePool(infosz);
    st = uefi_call_wrapper(f->GetInfo, 4, f, &fiGuid, &infosz, info);
    if (EFI_ERROR(st)) goto out;
    *size = info->FileSize;

    /* Allocate 2 MiB-aligned pages for the ARM64 Image + 64 KiB cmdline/dtb slack. */
    UINTN pages = EFI_SIZE_TO_PAGES(*size + OSV_IMAGE_ALIGN);
    EFI_PHYSICAL_ADDRESS addr = 0;
    st = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages,
                           EfiLoaderData, pages, &addr);
    if (EFI_ERROR(st)) goto out;
    /* Round up to 2 MiB alignment inside the allocation. */
    EFI_PHYSICAL_ADDRESS aligned =
        (addr + (OSV_IMAGE_ALIGN - 1)) & ~(EFI_PHYSICAL_ADDRESS)(OSV_IMAGE_ALIGN - 1);

    UINTN rd = *size;
    st = uefi_call_wrapper(f->Read, 3, f, &rd, (void *)aligned);
    if (EFI_ERROR(st)) goto out;
    if (rd != *size) { st = EFI_LOAD_ERROR; goto out; }
    *buf = (void *)aligned;

out:
    if (info) FreePool(info);
    uefi_call_wrapper(f->Close, 1, f);
    uefi_call_wrapper(root->Close, 1, root);
    return st;
}

/* ---- arch-specific kernel entry ------------------------------------------ */

#if defined(OSVEFI_ARCH_AARCH64)
/* Jump to the OSv ARM64 Image at image_base (phys), MMU off/EL1, x0 = dtb. */
static void osv_enter_kernel(void *image_base, void *dtb)
{
    register UINT64 x0 asm("x0") = (UINT64)dtb;        /* DTB phys -> x0 */
    register UINT64 x1 asm("x1") = (UINT64)image_base; /* entry -> x1 */
    asm volatile(
        "br  x1\n"
        :: "r"(x0), "r"(x1)
        : "memory");
    __builtin_unreachable();
}
#elif defined(OSVEFI_ARCH_X86_64)
static void osv_enter_kernel(void *image_base, void *dtb)
{
    (void)image_base; (void)dtb;
    /* x86 handoff differs (OSv 32-bit boot protocol); documented in README.md. */
}
#else
#error "define OSVEFI_ARCH_AARCH64 or OSVEFI_ARCH_X86_64"
#endif

EFI_STATUS osv_boot(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab)
{
    (void)systab;
    EFI_STATUS st;
    void *kernel = NULL;
    UINTN ksize = 0;

    print(L"OSv EFI stub: loading kernel from ESP\r\n");
    st = read_esp_file(image, OSV_KERNEL_PATH, &kernel, &ksize);
    if (EFI_ERROR(st)) {
        Print(L"OSv EFI stub: failed to read %s: %r\r\n", OSV_KERNEL_PATH, st);
        return st;
    }
    Print(L"OSv EFI stub: kernel %d bytes at %lx\r\n", (int)ksize, (UINT64)kernel);

    /* Validate the ARM64 Image magic "ARM\x64" at offset 0x38 (preboot.S _head). */
#if defined(OSVEFI_ARCH_AARCH64)
    {
        UINT8 *p = (UINT8 *)kernel;
        if (!(p[0x38] == 0x41 && p[0x39] == 0x52 &&
              p[0x3a] == 0x4d && p[0x3b] == 0x64)) {
            print(L"OSv EFI stub: WARNING kernel lacks ARM64 Image magic\r\n");
        } else {
            print(L"OSv EFI stub: ARM64 Image magic OK\r\n");
        }
    }
#endif

    /* Place the cmdline at image_base + 0x200 (preboot.S cmdline_addr). */
    {
        CHAR8 *cmd = NULL; UINTN cmdsz = 0;
        if (EFI_ERROR(read_esp_file(image, L"\\EFI\\BOOT\\cmdline",
                                    (void **)&cmd, &cmdsz)) || cmdsz == 0) {
            cmd = default_cmdline; cmdsz = sizeof(default_cmdline) - 1;
        }
        CHAR8 *dst = (CHAR8 *)kernel + OSV_CMDLINE_OFF;
        for (UINTN i = 0; i < cmdsz && i < 0x400; i++) dst[i] = cmd[i];
        dst[cmdsz < 0x400 ? cmdsz : 0x3ff] = 0;
        Print(L"OSv EFI stub: cmdline set (%d bytes)\r\n", (int)cmdsz);
    }

    /* Obtain a device tree. */
    void *dtb = find_config_table(&FdtTableGuid);
    if (dtb) {
        Print(L"OSv EFI stub: DTB from config table at %lx\r\n", (UINT64)dtb);
    } else {
        print(L"OSv EFI stub: no DTB config table; cannot boot OSv/aarch64 "
              L"(ACPI-only firmware - see README.md 'what remains')\r\n");
        return EFI_UNSUPPORTED;
    }

    /* GetMemoryMap + ExitBootServices (retry once if the key is stale). */
    UINTN msize = 0, mkey = 0, dsize = 0; UINT32 dver = 0;
    EFI_MEMORY_DESCRIPTOR *mmap = NULL;
    st = uefi_call_wrapper(BS->GetMemoryMap, 5, &msize, mmap, &mkey, &dsize, &dver);
    /* msize now holds required size; add slack for the next alloc. */
    msize += 4 * dsize;
    mmap = AllocatePool(msize);
    st = uefi_call_wrapper(BS->GetMemoryMap, 5, &msize, mmap, &mkey, &dsize, &dver);
    if (EFI_ERROR(st)) { Print(L"GetMemoryMap: %r\r\n", st); return st; }

    print(L"OSv EFI stub: ExitBootServices + jump to OSv\r\n");
    st = uefi_call_wrapper(BS->ExitBootServices, 2, image, mkey);
    if (EFI_ERROR(st)) {
        /* Key went stale between GetMemoryMap and ExitBootServices: re-fetch. */
        st = uefi_call_wrapper(BS->GetMemoryMap, 5, &msize, mmap, &mkey, &dsize, &dver);
        if (EFI_ERROR(st)) return st;
        st = uefi_call_wrapper(BS->ExitBootServices, 2, image, mkey);
        if (EFI_ERROR(st)) return st;
    }

    /* No boot services, no console from here on - jump to OSv. */
    osv_enter_kernel(kernel, dtb);
    return EFI_LOAD_ERROR; /* unreachable */
}
