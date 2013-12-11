/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_X86_PROCESSOR_H
#define ARCH_X86_PROCESSOR_H

#include <osv/types.h>

namespace processor {

constexpr ulong cr0_pe = 1u << 0;
constexpr ulong cr0_mp = 1u << 1;
constexpr ulong cr0_em = 1u << 2;
constexpr ulong cr0_ts = 1u << 3;
constexpr ulong cr0_et = 1u << 4;
constexpr ulong cr0_ne = 1u << 5;
constexpr ulong cr0_wp = 1u << 16;
constexpr ulong cr0_am = 1u << 18;
constexpr ulong cr0_nw = 1u << 29;
constexpr ulong cr0_cd = 1u << 30;
constexpr ulong cr0_pg = 1u << 31;

constexpr ulong cr4_vme = 1u << 0;
constexpr ulong cr4_pvi = 1u << 1;
constexpr ulong cr4_tsd = 1u << 2;
constexpr ulong cr4_de = 1u << 3;
constexpr ulong cr4_pse = 1u << 4;
constexpr ulong cr4_pae = 1u << 5;
constexpr ulong cr4_mce = 1u << 6;
constexpr ulong cr4_pge = 1u << 7;
constexpr ulong cr4_pce = 1u << 8;
constexpr ulong cr4_osfxsr = 1u << 9;
constexpr ulong cr4_osxmmexcpt = 1u << 10;
constexpr ulong cr4_vmxe = 1u << 13;
constexpr ulong cr4_smxe = 1u << 14;
constexpr ulong cr4_fsgsbase = 1u << 16;
constexpr ulong cr4_pcide = 1u << 17;
constexpr ulong cr4_osxsave = 1u << 18;
constexpr ulong cr4_smep = 1u << 20;

constexpr ulong rflags_if = 1u << 9;

struct cpuid_result {
    u32 a, b, c, d;
};

inline cpuid_result cpuid(u32 function) {
    cpuid_result r;
    asm("cpuid" : "=a"(r.a), "=b"(r.b), "=c"(r.c), "=d"(r.d)
                : "0"(function));
    return r;
}

inline cpuid_result cpuid(u32 function, u32 subleaf) {
    cpuid_result r;
    asm("cpuid" : "=a"(r.a), "=b"(r.b), "=c"(r.c), "=d"(r.d)
                : "0"(function), "2"(subleaf));
    return r;
}

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

inline void ltr(u16 tr) {
    asm volatile("ltr %0" : : "rm"(tr));
}

inline u16 str() {
    u16 tr;
    asm volatile("str %0" : "=rm"(tr));
    return tr;
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

inline bool wrmsr_safe(u32 index, u64 data) {
    u32 lo = data, hi = data >> 32;

    bool ret = true;
    asm volatile ("1: \n\t"
                  "wrmsr\n\t"
                  "2: \n\t"
                  ".pushsection .text.fixup, \"ax\" \n\t"
                  "3: \n\t"
                  "xor %[ret], %[ret]\n\t"
                  "jmp 2b \n\t"
                  ".popsection \n\t"
                  ".pushsection .fixup, \"a\" \n\t"
                  ".quad 1b, 3b \n\t"
                  ".popsection\n"
            :  [ret]"+r"(ret)
            : "c"(index), "a"(lo), "d"(hi));

    return ret;
}

inline void wrfsbase(u64 data)
{
    asm volatile("wrfsbase %0" : : "r"(data));
}

inline void halt_no_interrupts() {
    asm volatile ("cli; hlt" : : : "memory");
}

inline void sti_hlt() {
    asm volatile ("sti; hlt" : : : "memory");
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
    asm volatile ("sti" : : : "memory");
}

inline void cli()
{
    asm volatile ("cli" : : : "memory");
}

__attribute__((no_instrument_function))
inline void cli_notrace();

inline void cli_notrace()
{
    asm volatile ("cli" : : : "memory");
}

inline u64 rdtsc()
{
    u32 lo, hi;
    asm("rdtsc" : "=a"(lo), "=d"(hi));
    return lo | (u64(hi) << 32);
}

struct fpu_state {
    char x[512];
    char extra[];
};

inline void fxsave(fpu_state* s)
{
    asm volatile("fxsaveq %0" : "=m"(*s));
}

inline void fxrstor(fpu_state* s)
{
    asm volatile("fxrstorq %0" : : "m"(*s));
}

inline void xsave(fpu_state* s, u64 mask)
{
    u32 a = mask, d = mask >> 32;
    asm volatile("xsaveq %[fpu]" : [fpu]"=m"(*s) : "a"(a), "d"(d));
}

inline void xsaveopt(fpu_state* s, u64 mask)
{
    u32 a = mask, d = mask >> 32;
    asm volatile("xsaveoptq %[fpu]" : [fpu]"=m"(*s) : "a"(a), "d"(d));
}

inline void xrstor(const fpu_state* s, u64 mask)
{
    u32 a = mask, d = mask >> 32;
    asm volatile("xrstorq %[fpu]" : : [fpu]"m"(*s), "a"(a), "d"(d));
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
