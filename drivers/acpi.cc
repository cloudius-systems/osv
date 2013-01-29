extern "C" {
    #include "acpi.h"
    #include "acpiosxf.h"
    #include "acpixf.h"
}
#include <stdlib.h>
#include "mmu.hh"
#include "sched.hh"
#include "drivers/clock.hh"
#include "processor.hh"

ACPI_STATUS AcpiOsInitialize(void)
{
    return AE_OK;
}

ACPI_STATUS AcpiOsTerminate(void)
{
    return AE_OK;
}

ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer(void)
{
    ACPI_SIZE rsdp;
    auto st = AcpiFindRootPointer(&rsdp);
    if (ACPI_FAILURE(st)) {
        abort();
    }
    return rsdp;
}

ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *InitVal,
        ACPI_STRING *NewVal)
{
    *NewVal = nullptr;
    return AE_OK;
}

ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER *ExistingTable,
        ACPI_TABLE_HEADER **NewTable)
{
    *NewTable = nullptr;
    return AE_OK;
}

ACPI_STATUS AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER *ExistingTable,
    ACPI_PHYSICAL_ADDRESS *NewAddress, UINT32 *NewTableLength)
{
    *NewAddress = 0;
    *NewTableLength = 0;
    return AE_OK;
}

ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle)
{
    // FIXME: implement
    return AE_NOT_IMPLEMENTED;
}

ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK Handle)
{
    // FIXME: implement
    return 0;
}

void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags)
{
    // FIXME: implement
}

void AcpiOsDeleteLock(ACPI_SPINLOCK Handle)
{
    // FIXME: implement
}

ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits,
        UINT32 InitialUnits, ACPI_SEMAPHORE *OutHandle)
{
    // FIXME: implement
    return AE_NOT_IMPLEMENTED;
}

ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle)
{
    return AE_NOT_IMPLEMENTED;
}

ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle,
        UINT32 Units, UINT16 Timeout)
{
    return AE_NOT_IMPLEMENTED;
}

ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units)
{
    return AE_NOT_IMPLEMENTED;
}

void *AcpiOsAllocate(ACPI_SIZE Size)
{
    return malloc(Size);
}

void AcpiOsFree(void *Memory)
{
    free(Memory);
}

void *AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS Where, ACPI_SIZE Length)
{
    return mmu::phys_to_virt(Where);
}

void AcpiOsUnmapMemory(void *LogicalAddress, ACPI_SIZE Size)
{
}

ACPI_STATUS AcpiOsGetPhysicalAddress(void *LogicalAddress,
        ACPI_PHYSICAL_ADDRESS *PhysicalAddress)
{
    *PhysicalAddress = mmu::virt_to_phys(LogicalAddress);
    return AE_OK;
}

#if 0
ACPI_STATUS AcpiOsCreateCache (
    char                    *CacheName,
    UINT16                  ObjectSize,
    UINT16                  MaxDepth,
    ACPI_CACHE_T            **ReturnCache);

ACPI_STATUS
AcpiOsDeleteCache (
    ACPI_CACHE_T            *Cache);

ACPI_STATUS
AcpiOsPurgeCache (
    ACPI_CACHE_T            *Cache);

void *
AcpiOsAcquireObject (
    ACPI_CACHE_T            *Cache);

ACPI_STATUS
AcpiOsReleaseObject (
    ACPI_CACHE_T            *Cache,
    void                    *Object);

#endif

/*
 * Interrupt handlers
 */
ACPI_STATUS
AcpiOsInstallInterruptHandler(
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine,
    void                    *Context)
{
    return AE_NOT_IMPLEMENTED;
}

ACPI_STATUS
AcpiOsRemoveInterruptHandler(
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine)
{
    return AE_NOT_IMPLEMENTED;
}

ACPI_THREAD_ID AcpiOsGetThreadId(void)
{
    return reinterpret_cast<uintptr_t>(sched::thread::current());
}

ACPI_STATUS AcpiOsExecute(
    ACPI_EXECUTE_TYPE       Type,
    ACPI_OSD_EXEC_CALLBACK  Function,
    void                    *Context)
{
    return AE_NOT_IMPLEMENTED;
}

void AcpiOsWaitEventsComplete(void)
{
    // FIXME: ?
}

void AcpiOsSleep(UINT64 Milliseconds)
{
    sched::thread::sleep_until(clock::get()->time() + Milliseconds * 1000000);
}

void AcpiOsStall(UINT32 Microseconds)
{
    // spec says to spin, but...
    sched::thread::sleep_until(clock::get()->time() + u64(Microseconds) * 1000);
}

ACPI_STATUS AcpiOsReadPort(
    ACPI_IO_ADDRESS         Address,
    UINT32                  *Value,
    UINT32                  Width)
{
    switch (Width) {
    case 8:
        *Value = processor::inb(Address);
        break;
    case 16:
        *Value = processor::inw(Address);
        break;
    case 32:
        *Value = processor::inl(Address);
        break;
    default:
        return AE_BAD_PARAMETER;
    }
    return AE_OK;
}

ACPI_STATUS AcpiOsWritePort(
    ACPI_IO_ADDRESS         Address,
    UINT32                  Value,
    UINT32                  Width)
{
    switch (Width) {
    case 8:
        processor::outb(Value, Address);
        break;
    case 16:
        processor::outw(Value, Address);
        break;
    case 32:
        processor::outl(Value, Address);
        break;
    default:
        return AE_BAD_PARAMETER;
    }
    return AE_OK;
}


ACPI_STATUS
AcpiOsReadMemory (
    ACPI_PHYSICAL_ADDRESS   Address,
    UINT64                  *Value,
    UINT32                  Width)
{
    switch (Width) {
    case 8:
        *Value = *mmu::phys_cast<u8>(Address);
        break;
    case 16:
        *Value = *mmu::phys_cast<u16>(Address);
        break;
    case 32:
        *Value = *mmu::phys_cast<u32>(Address);
        break;
    case 64:
        *Value = *mmu::phys_cast<u64>(Address);
        break;
    default:
        return AE_BAD_PARAMETER;
    }
    return AE_OK;
}

ACPI_STATUS
AcpiOsWriteMemory (
    ACPI_PHYSICAL_ADDRESS   Address,
    UINT64                  Value,
    UINT32                  Width)
{
    switch (Width) {
    case 8:
        *mmu::phys_cast<u8>(Address) = Value;
        break;
    case 16:
        *mmu::phys_cast<u16>(Address) = Value;
        break;
    case 32:
        *mmu::phys_cast<u32>(Address) = Value;
        break;
    case 64:
        *mmu::phys_cast<u64>(Address) = Value;
        break;
    default:
        return AE_BAD_PARAMETER;
    }
    return AE_OK;
}

ACPI_STATUS
AcpiOsReadPciConfiguration(
    ACPI_PCI_ID             *PciId,
    UINT32                  Reg,
    UINT64                  *Value,
    UINT32                  Width)
{
    return AE_NOT_IMPLEMENTED;
}

ACPI_STATUS
AcpiOsWritePciConfiguration (
    ACPI_PCI_ID             *PciId,
    UINT32                  Reg,
    UINT64                  Value,
    UINT32                  Width)
{
    return AE_NOT_IMPLEMENTED;
}

BOOLEAN
AcpiOsReadable(void *Pointer, ACPI_SIZE Length)
{
    return true;
}

BOOLEAN
AcpiOsWritable(void *Pointer, ACPI_SIZE Length)
{
    return true;
}

UINT64 AcpiOsGetTimer(void)
{
    return clock::get()->time() / 100;
}

ACPI_STATUS AcpiOsSignal(UINT32 Function, void *Info)
{
    abort();
}

void ACPI_INTERNAL_VAR_XFACE AcpiOsPrintf(const char *Format, ...)
{
    va_list va;
    va_start(va, Format);
    vprintf(Format, va);
    va_end(va);
}

void AcpiOsVprintf(const char *Format, va_list Args)
{
    vprintf(Format, Args);
}

