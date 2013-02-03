#ifndef ARCH_X86_PROCESSOR_H
#define ARCH_X86_PROCESSOR_H

#include "types.hh"

namespace processor {

	inline ulong read_cr0() {
	    ulong r;
	    asm volatile ("mov %%cr0, %0" : "=r"(r));
	    return r;
	}

	inline void write_cr0(ulong r) {
	    asm volatile ("mov %0, %%cr0" : : "r"(r));
	}

	inline ulong read_cr2() {
	    ulong r;
	    asm volatile ("mov %%cr2, %0" : "=r"(r));
	    return r;
	}

	inline void write_cr2(ulong r) {
	    asm volatile ("mov %0, %%cr2" : : "r"(r));
	}

	inline ulong read_cr3() {
	    ulong r;
	    asm volatile ("mov %%cr3, %0" : "=r"(r));
	    return r;
	}

	inline void write_cr3(ulong r) {
	    asm volatile ("mov %0, %%cr3" : : "r"(r));
	}

	inline ulong read_cr4() {
	    ulong r;
	    asm volatile ("mov %%cr4, %0" : "=r"(r));
	    return r;
	}

	inline void write_cr4(ulong r) {
	    asm volatile ("mov %0, %%cr4" : : "r"(r));
	}

	inline ulong read_cr8() {
	    ulong r;
	    asm volatile ("mov %%cr8, %0" : "=r"(r));
	    return r;
	}

	inline void write_cr8(ulong r) {
	    asm volatile ("mov %0, %%cr8" : : "r"(r));
	}

	struct desc_ptr {
	    desc_ptr(u16 limit, ulong addr) : limit(limit), addr(addr) {}
	    u16 limit;
	    ulong addr;
	} __attribute__((packed));

	inline void lgdt(const desc_ptr& ptr) {
	    asm volatile ("lgdt %0" : : "m"(ptr));
	}

	inline void sgdt(desc_ptr& ptr) {
	    asm volatile ("sgdt %0" : "=m"(ptr));
	}

	inline void lidt(const desc_ptr& ptr) {
	    asm volatile ("lidt %0" : : "m"(ptr));
	}

	inline void sidt(desc_ptr& ptr) {
	    asm volatile ("sidt %0" : "=m"(ptr));
	}

	inline u16 read_cs() {
	    u16 r;
	    asm volatile ("mov %%cs, %0" : "=rm"(r));
	    return r;
	}

	inline u16 read_ds() {
	    u16 r;
	    asm volatile ("mov %%ds, %0" : "=rm"(r));
	    return r;
	}

	inline void write_ds(u16 r) {
	    asm volatile ("mov %0, %%ds" : : "rm"(r));
	}

	inline u16 read_es() {
	    u16 r;
	    asm volatile ("mov %%es, %0" : "=rm"(r));
	    return r;
	}

	inline void write_es(u16 r) {
	    asm volatile ("mov %0, %%es" : : "rm"(r));
	}

	inline u16 read_fs() {
	    u16 r;
	    asm volatile ("mov %%fs, %0" : "=rm"(r));
	    return r;
	}

	inline void write_fs(u16 r) {
	    asm volatile ("mov %0, %%fs" : : "rm"(r));
	}

	inline u16 read_gs() {
	    u16 r;
	    asm volatile ("mov %%gs, %0" : "=rm"(r));
	    return r;
	}

	inline void write_gs(u16 r) {
	    asm volatile ("mov %0, %%gs" : : "rm"(r));
	}

	inline u16 read_ss() {
	    u16 r;
	    asm volatile ("mov %%ss, %0" : "=rm"(r));
	    return r;
	}

	inline void write_ss(u16 r) {
	    asm volatile ("mov %0, %%ss" : : "rm"(r));
	}

	inline u64 rdmsr(u32 index) {
	    u32 lo, hi;
	    asm volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(index));
	    return lo | ((u64)hi << 32);
	}

	inline void wrmsr(u32 index, u64 data) {
	    u32 lo = data, hi = data >> 32;
	    asm volatile ("wrmsr" : : "c"(index), "a"(lo), "d"(hi));
	}

	inline void halt_no_interrupts() {
	    asm volatile ("cli; hlt");
	}

	inline u8 inb (u16 port)
	{
		u8 r;
		asm volatile ("inb %1, %0":"=a" (r):"dN" (port));
		return r;
	}

	inline u16 inw (u16 port)
	{
		u16 r;
		asm volatile ("inw %1, %0":"=a" (r):"dN" (port));
		return r;
	}

	inline u32 inl (u16 port)
	{
		u32 r;
		asm volatile ("inl %1, %0":"=a" (r):"dN" (port));
		return r;
	}

	inline void outb (u8 val, u16 port)
	{
		asm volatile ("outb %0, %1"::"a" (val), "dN" (port));

	}

	inline void outw (u16 val, u16 port)
	{
		asm volatile ("outw %0, %1"::"a" (val), "dN" (port));

	}

	inline void outl (u32 val, u16 port)
	{
		asm volatile ("outl %0, %1"::"a" (val), "dN" (port));

	}

	inline void sti()
	{
	    asm volatile ("sti");
	}

	inline void cli()
	{
	    asm volatile ("cli");
	}

	inline u64 rdtsc()
	{
	    u32 lo, hi;
	    asm("rdtsc" : "=a"(lo), "=d"(hi));
	    return lo | (u64(hi) << 32);
	}

	struct task_state_segment {
	    u32 reserved0;
	    u64 rsp[3];
	    u64 ist[8];   // ist[0] is reserved
	    u32 reserved1;
	    u32 reserved2;
	    u16 reserved3;
	    u16 io_bitmap_base;
	} __attribute__((packed));

	struct aligned_task_state_segment {
	    u32 pad;  // force 64-bit structures to be aligned
	    task_state_segment tss;
	} __attribute__((packed, aligned(8)));
};

#endif
