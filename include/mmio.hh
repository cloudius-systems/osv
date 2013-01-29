#ifndef MMIO_HH
#define MMIO_HH

#include "types.hh"

typedef volatile void* mmioaddr_t;

const mmioaddr_t mmio_nullptr = 0;

// Functions used to access mmio regions
void mmio_setb(mmioaddr_t addr, u8 val);
void mmio_setw(mmioaddr_t addr, u16 val);
void mmio_setl(mmioaddr_t addr, u32 val);
void mmio_setq(mmioaddr_t addr, u64 val);
u8 mmio_getb(mmioaddr_t addr);
u16 mmio_getw(mmioaddr_t addr);
u32 mmio_getl(mmioaddr_t addr);
u64 mmio_getq(mmioaddr_t addr);

// Map mmio regions
mmioaddr_t mmio_map(u64 paddr, size_t size_bytes);
void mmio_unmap(mmioaddr_t addr, size_t size_bytes);

#endif // MMIO_HH
