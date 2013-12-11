/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef MMU_HH
#define MMU_HH

#include "fs/fs.hh"
#include <stdint.h>
#include <boost/intrusive/set.hpp>
#include <osv/types.h>
#include <functional>
#include <osv/error.h>

struct exception_frame;
/**
 * MMU namespace
 */
namespace mmu {

constexpr uintptr_t page_size = 4096;

constexpr int pte_per_page = 512;
constexpr uintptr_t huge_page_size = mmu::page_size*pte_per_page; // 2 MB

typedef uint64_t f_offset;

static char* const phys_mem = reinterpret_cast<char*>(0xffffc00000000000);
// area for debug allocations:
static char* const debug_base = reinterpret_cast<char*>(0xffffe00000000000);

inline unsigned pt_index(void *virt, unsigned level)
{
    auto v = reinterpret_cast<ulong>(virt);
    return (v >> (12 + level * 9)) & 511;
}

enum {
    perm_read = 1,
    perm_write = 2,
    perm_exec = 4,
    perm_rx = perm_read | perm_exec,
    perm_rw = perm_read | perm_write,
    perm_rwx = perm_read | perm_write | perm_exec,
};

class addr_range {
public:
    addr_range(uintptr_t start, uintptr_t end)
        : _start(start), _end(end) {}
    uintptr_t start() const { return _start; }
    uintptr_t end() const { return _end; }
private:
    uintptr_t _start;
    uintptr_t _end;
};

enum {
    page_fault_prot  = 1ul << 0,
    page_fault_write = 1ul << 1,
    page_fault_user  = 1ul << 2,
    page_fault_rsvd  = 1ul << 3,
    page_fault_insn  = 1ul << 4,
};

enum {
    mmap_fixed       = 1ul << 0,
    mmap_populate    = 1ul << 1,
    mmap_shared      = 1ul << 2,
    mmap_uninitialized = 1ul << 3,
};

class vma {
public:
    vma(addr_range range, unsigned perm, unsigned flags);
    virtual ~vma();
    void set(uintptr_t start, uintptr_t end);
    void protect(unsigned perm);
    uintptr_t start() const;
    uintptr_t end() const;
    void* addr() const;
    uintptr_t size() const;
    unsigned perm() const;
    virtual void fault(uintptr_t addr, exception_frame *ef) = 0;
    virtual void split(uintptr_t edge) = 0;
    virtual error sync(uintptr_t start, uintptr_t end) = 0;
    class addr_compare;
protected:
    addr_range _range;
    unsigned _perm;
    unsigned _flags;
public:
    boost::intrusive::set_member_hook<> _vma_list_hook;
};

// compare object for searching the vma list
// defines a partial ordering: if a range intersects a vma,
// it is considered equal, if it is completely before it is less
// than the vma, if it is completely after it is after the vma.
//
// this partial ordering is compatible with vma_list_type.
class vma::addr_compare {
public:
    bool operator()(const vma& x, addr_range y) const { return x.end() <= y.start(); }
    bool operator()(addr_range x, const vma& y) const { return x.end() <= y.start(); }
};

class anon_vma : public vma {
public:
    anon_vma(addr_range range, unsigned perm, unsigned flags);
    virtual void split(uintptr_t edge) override;
    virtual error sync(uintptr_t start, uintptr_t end) override;
    virtual void fault(uintptr_t addr, exception_frame *ef) override;
};

class file_vma : public vma {
public:
    file_vma(addr_range range, unsigned perm, fileref file, f_offset offset, bool shared);
    virtual void split(uintptr_t edge) override;
    virtual error sync(uintptr_t start, uintptr_t end) override;
    virtual void fault(uintptr_t addr, exception_frame *ef) override;
private:
    f_offset offset(uintptr_t addr);
    fileref _file;
    f_offset _offset;
    bool _shared;
};

void* map_file(void* addr, size_t size, unsigned flags, unsigned perm,
              fileref file, f_offset offset);
void* map_anon(void* addr, size_t size, unsigned flags, unsigned perm);
void unmap(void* addr, size_t size);
int protect(void *addr, size_t size, unsigned int perm);
error msync(void* addr, size_t length, int flags);
bool is_linear_mapped(void *addr, size_t size);
bool ismapped(void *addr, size_t size);
bool isreadable(void *addr, size_t size);

typedef uint64_t phys;
phys virt_to_phys(void *virt);
void* phys_to_virt(phys pa);

template <typename T>
T* phys_cast(phys pa)
{
    return static_cast<T*>(phys_to_virt(pa));
}

void linear_map(void* virt, phys addr, size_t size,
                size_t slop = mmu::page_size);
void free_initial_memory_range(uintptr_t addr, size_t size);
void switch_to_runtime_page_table();
void set_nr_page_sizes(unsigned nr);

void vpopulate(void* addr, size_t size);
void vdepopulate(void* addr, size_t size);

}

#endif
