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
#include <osv/addr_range.hh>
#include <unordered_map>
#include <memory>

struct exception_frame;
class balloon;
typedef std::shared_ptr<balloon> balloon_ptr;

/**
 * MMU namespace
 */
namespace mmu {

constexpr uintptr_t page_size = 4096;
constexpr int page_size_shift = 12; // log2(page_size)

constexpr int pte_per_page = 512;
constexpr int pte_per_page_shift = 9; // log2(pte_per_page)

constexpr uintptr_t huge_page_size = mmu::page_size*pte_per_page; // 2 MB

typedef uint64_t f_offset;

static char* const phys_mem = reinterpret_cast<char*>(0xffffc00000000000);
// area for debug allocations:
static char* const debug_base = reinterpret_cast<char*>(0xffffe00000000000);

constexpr inline unsigned pt_index(void *virt, unsigned level)
{
    return (reinterpret_cast<ulong>(virt) >> (page_size_shift + level * pte_per_page_shift)) & (pte_per_page - 1);
}

enum {
    perm_read = 1,
    perm_write = 2,
    perm_exec = 4,
    perm_rx = perm_read | perm_exec,
    perm_rw = perm_read | perm_write,
    perm_rwx = perm_read | perm_write | perm_exec,
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
    mmap_jvm_heap    = 1ul << 4,
    mmap_small       = 1ul << 5,
    mmap_jvm_balloon = 1ul << 6,
};

struct page_allocator;

class vma {
public:
    vma(addr_range range, unsigned perm, unsigned flags, bool map_dirty, page_allocator *page_ops = nullptr);
    virtual ~vma();
    void set(uintptr_t start, uintptr_t end);
    void protect(unsigned perm);
    uintptr_t start() const;
    uintptr_t end() const;
    void* addr() const;
    uintptr_t size() const;
    unsigned perm() const;
    unsigned flags() const;
    virtual void fault(uintptr_t addr, exception_frame *ef);
    virtual void split(uintptr_t edge) = 0;
    virtual error sync(uintptr_t start, uintptr_t end) = 0;
    virtual int validate_perm(unsigned perm) { return 0; }
    virtual page_allocator* page_ops();
    void update_flags(unsigned flag);
    bool has_flags(unsigned flag);
    template<typename T> ulong operate_range(T mapper, void *start, size_t size);
    template<typename T> ulong operate_range(T mapper);
    bool map_dirty();
    class addr_compare;
protected:
    addr_range _range;
    unsigned _perm;
    unsigned _flags;
    bool _map_dirty;
    page_allocator *_page_ops;
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
};

class file_vma : public vma {
public:
    file_vma(addr_range range, unsigned perm, fileref file, f_offset offset, bool shared, page_allocator *page_ops);
    ~file_vma();
    virtual void split(uintptr_t edge) override;
    virtual error sync(uintptr_t start, uintptr_t end) override;
    virtual int validate_perm(unsigned perm);
private:
    f_offset offset(uintptr_t addr);
    fileref _file;
    f_offset _offset;
    bool _shared;
};

ulong map_jvm(unsigned char* addr, size_t size, size_t align, balloon_ptr b);

class jvm_balloon_vma : public vma {
public:
    jvm_balloon_vma(unsigned char *jvm_addr, uintptr_t start, uintptr_t end, balloon_ptr b, unsigned perm, unsigned flags);
    virtual ~jvm_balloon_vma();
    virtual void split(uintptr_t edge) override;
    virtual error sync(uintptr_t start, uintptr_t end) override;
    virtual void fault(uintptr_t addr, exception_frame *ef) override;
    void detach_balloon();
    unsigned char *jvm_addr() { return _jvm_addr; }
    friend ulong map_jvm(unsigned char* jvm_addr, size_t size, size_t align, balloon_ptr b);
protected:
    balloon_ptr _balloon;
    unsigned char *_jvm_addr;
private:
    unsigned _real_perm;
    unsigned _real_flags;
};

class shm_file final : public special_file {
    size_t _size;
    std::unordered_map<uintptr_t, void*> _pages;
public:
    shm_file(size_t size, int flags);
    virtual int stat(struct stat* buf) override;
    virtual int close() override;
    virtual std::unique_ptr<file_vma> mmap(addr_range range, unsigned flags, unsigned perm, off_t offset) override;
    virtual void* get_page(uintptr_t start, uintptr_t offset, size_t size) override;
    virtual void put_page(void *addr, uintptr_t start, uintptr_t offset, size_t size) override;
};

void* map_file(const void* addr, size_t size, unsigned flags, unsigned perm,
              fileref file, f_offset offset);
void* map_anon(const void* addr, size_t size, unsigned flags, unsigned perm);
ulong map_jvm(const void* addr, size_t size, balloon *b);

error munmap(const void* addr, size_t size);
error mprotect(const void *addr, size_t size, unsigned int perm);
error msync(const void* addr, size_t length, int flags);
error mincore(const void *addr, size_t length, unsigned char *vec);
bool is_linear_mapped(const void *addr, size_t size);
bool ismapped(const void *addr, size_t size);
bool isreadable(void *addr, size_t size);
std::unique_ptr<file_vma> default_file_mmap(file* file, addr_range range, unsigned flags, unsigned perm, off_t offset);
std::unique_ptr<file_vma> map_file_mmap(file* file, addr_range range, unsigned flags, unsigned perm, off_t offset);

void unmap_address(void *addr, size_t size);
void add_mapping(void *buf_addr, uintptr_t offset, uintptr_t vaddr);
bool remove_mapping(void *buf_addr, void *paddr, uintptr_t addr);

typedef uint64_t phys;
phys virt_to_phys(void *virt);
void* phys_to_virt(phys pa);

template <typename T>
T* phys_cast(phys pa)
{
    return static_cast<T*>(phys_to_virt(pa));
}

inline
bool is_page_aligned(intptr_t addr)
{
    return !(addr & (page_size-1));
}

inline
bool is_page_aligned(void* addr)
{
    return is_page_aligned(reinterpret_cast<intptr_t>(addr));
}

void linear_map(void* virt, phys addr, size_t size,
                size_t slop = mmu::page_size);
void free_initial_memory_range(uintptr_t addr, size_t size);
void switch_to_runtime_page_table();
void set_nr_page_sizes(unsigned nr);

void vpopulate(void* addr, size_t size);
void vdepopulate(void* addr, size_t size);
void vcleanup(void* addr, size_t size);

void vm_fault(uintptr_t addr, exception_frame* ef);

std::string procfs_maps();

}

#endif
