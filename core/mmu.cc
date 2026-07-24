/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/mmu.hh>
#include <osv/mempool.hh>
#include <osv/sched.hh>
#include "processor.hh"
#include <osv/debug.hh>
#include "exceptions.hh"
#include <string.h>
#include <iterator>
#include "libc/signal.hh"
#include <osv/align.hh>
#include <osv/ilog2.hh>
#include <osv/prio.hh>
#include <safe-ptr.hh>
#include "fs/vfs/vfs.h"
#include <osv/error.h>
#include <osv/trace.hh>
#include <stack>
#include <fs/fs.hh>
#include <osv/file.h>
#include "dump.hh"
#include <osv/rcu.hh>
#include <osv/rwlock.h>
#include <algorithm>
#include <numeric>
#include <set>
#include <vector>

#include <osv/kernel_config_memory_debug.h>
#include <osv/kernel_config_lazy_stack.h>
#include <osv/kernel_config_lazy_stack_invariant.h>
#include <osv/kernel_config_memory_jvm_balloon.h>
#include <osv/kernel_config_fork.h>
#if CONF_fork
#include <osv/fork_arena.hh>
#endif

// FIXME: Without this pragma, we get a lot of warnings that I don't know
// how to explain or fix. For now, let's just ignore them :-(
#pragma GCC diagnostic ignored "-Wstringop-overflow"

extern void* elf_start;
extern size_t elf_size;

extern const char text_start[], text_end[];

namespace mmu {

#if CONF_lazy_stack
// We need to ensure that lazy stack is populated deeply enough (2 pages)
// for all the cases when the vma_list_mutex is taken for write to prevent
// page faults triggered on stack. The page-fault handling logic would
// attempt to take same vma_list_mutex fo read and end up with a deadlock.
#define PREVENT_STACK_PAGE_FAULT \
    arch::ensure_next_two_stack_pages();
#else
#define PREVENT_STACK_PAGE_FAULT
#endif

struct vma_range_compare {
    bool operator()(const vma_range& a, const vma_range& b) const {
        return a.start() < b.start();
    }
};

//Set of all vma ranges - both linear and non-linear ones
typedef std::set<vma_range, vma_range_compare> vma_range_set_type;
__attribute__((init_priority((int)init_prio::vma_range_set)))
vma_range_set_type vma_range_set;
rwlock_t vma_range_set_mutex;

struct linear_vma_compare {
    bool operator()(const linear_vma* a, const linear_vma* b) const {
        return a->_virt_addr < b->_virt_addr;
    }
};

__attribute__((init_priority((int)init_prio::linear_vma_set)))
std::set<linear_vma*, linear_vma_compare> linear_vma_set;
rwlock_t linear_vma_set_mutex;

namespace bi = boost::intrusive;

class vma_compare {
public:
    bool operator ()(const vma& a, const vma& b) const {
        return a.addr() < b.addr();
    }
};

constexpr uintptr_t lower_vma_limit = 0x0;
constexpr uintptr_t upper_vma_limit = 0x400000000000;

typedef boost::intrusive::set<vma,
                              bi::compare<vma_compare>,
                              bi::member_hook<vma,
                                              bi::set_member_hook<>,
                                              &vma::_vma_list_hook>,
                              bi::optimize_size<true>
                              > vma_list_base;

struct vma_list_type : vma_list_base {
    // The edge markers are inserted into `into` (defaults to the global
    // vma_range_set / mutex).  A fork child's private vma_list points `into`
    // at the child's OWN range set so the child's edges never pollute the
    // global set (see clone_address_space / address_space child ctor).
    vma_list_type(vma_range_set_type *into = &vma_range_set,
                  rwlock_t *into_mutex = &vma_range_set_mutex) {
        // insert markers for the edges of allocatable area
        // simplifies searches
        auto lower_edge = new anon_vma(addr_range(lower_vma_limit, lower_vma_limit), 0, 0);
        insert(*lower_edge);
        auto upper_edge = new anon_vma(addr_range(upper_vma_limit, upper_vma_limit), 0, 0);
        insert(*upper_edge);

        WITH_LOCK(into_mutex->for_write()) {
            into->insert(vma_range(lower_edge));
            into->insert(vma_range(upper_edge));
        }
    }
};

__attribute__((init_priority((int)init_prio::vma_list)))
vma_list_type vma_list;

// protects vma list and page table modifications.
// anything that may add, remove, split vma, zaps pte or changes pte permission
// should hold the lock for write
rwlock_t vma_list_mutex;

#if CONF_fork
// -----------------------------------------------------------------------------
// Stage 2 fork: per-process address space object.
//
// An address_space bundles a page-table root (PML4) with its own vma_list.
// "AS0" (kernel_as) aliases the pre-existing global vma_list / vma_list_mutex
// and uses the arch page_table_root, so existing behaviour is unchanged: the
// kernel and the initial application run in AS0.
//
// A child address_space (clone_address_space, for fork) owns a private PML4
// whose *kernel half* (the PML4 slots that map OSv text/data and the
// identity/phys ranges) is shared with AS0, while its *application half* is a
// COW clone of the parent's page tables.  It also owns a private vma_list that
// is a structural copy of the parent's.
//
// PML4 slot layout on x86-64 (each slot spans 512 GB):
//   slot 0        : OSv kernel text/data (mapped at ~1 GB) -- SHARED
//   slots 1..127  : application VMA space (ELF at slot 32, mmap at slot 64) -- PRIVATE (COW)
//   slots 128..511: kernel identity / phys / mempool / debug maps -- SHARED
// Only slots 1..127 are cloned per child; the rest are shared by copying the
// parent's PML4 entries (pointing at the same lower-level tables).
constexpr unsigned pml4_app_first = 1;
constexpr unsigned pml4_app_last = 127;   // inclusive

struct address_space {
    vma_list_type *vmas;      // AS0: aliases global vma_list; child: owns_vmas
    rwlock_t *vmas_mutex;     // AS0: aliases global vma_list_mutex
    // Per-AS set of vma ranges used by the allocation path (find_hole etc.).
    // AS0 aliases the global vma_range_set / mutex; a child owns private ones
    // (owned_ranges/owned_ranges_mutex) so its post-fork mmap picks a hole from
    // -- and inserts into -- its OWN address space, not the global set.
    vma_range_set_type *ranges;   // AS0: aliases global vma_range_set
    rwlock_t *ranges_mutex;       // AS0: aliases global vma_range_set_mutex
    // Synthetic top-level entry (mirrors the arch page_table_root): its
    // next_pt_addr() is the physical address of this AS's PML4 page.  The page
    // table walk starts here via get_root_pt() (see map_range).  Null for AS0
    // (which uses the arch page_table_root).
    pt_element<4> *top;       // -> &_top for a child; nullptr for AS0
    pt_element<4> _top;       // storage for the synthetic entry (child only)
    phys pt_root;             // phys of PML4 page (CR3 value); 0 == arch root
    bool is_kernel;

    // Storage owned by a child AS.
    std::unique_ptr<vma_list_type> owned_vmas;
    std::unique_ptr<rwlock_t> owned_mutex;
    std::unique_ptr<vma_range_set_type> owned_ranges;
    std::unique_ptr<rwlock_t> owned_ranges_mutex;

    // AS0 constructor: alias the globals.
    address_space(vma_list_type *l, rwlock_t *m, phys root, bool kernel)
        : vmas(l), vmas_mutex(m), ranges(&vma_range_set),
          ranges_mutex(&vma_range_set_mutex), top(nullptr), pt_root(root),
          is_kernel(kernel) {}

    // Child constructor: owns a fresh vma_list, mutex and PML4 page.  The
    // synthetic top entry points at the child PML4 (phys pml4_page_phys).
    address_space(phys pml4_page_phys)
        : pt_root(pml4_page_phys), is_kernel(false)
        , owned_mutex(new rwlock_t())
        , owned_ranges(new vma_range_set_type())
        , owned_ranges_mutex(new rwlock_t())
    {
        // The child's private vma_list must insert its edge markers into the
        // child's OWN range set, not the global one -- so build owned_ranges
        // first and hand it to the vma_list ctor.
        ranges = owned_ranges.get();
        ranges_mutex = owned_ranges_mutex.get();
        owned_vmas.reset(new vma_list_type(ranges, ranges_mutex));
        _top = make_intermediate_pte(hw_ptep<4>::force(&_top), pml4_page_phys);
        top = &_top;
        vmas = owned_vmas.get();
        vmas_mutex = owned_mutex.get();
    }
};

// AS0: kernel + initial application.  Aliases the global vma_list.  pt_root is
// filled in lazily (0 means "arch page_table_root", see pt_root_phys()).
__attribute__((init_priority((int)init_prio::vma_list)))
address_space kernel_as{&vma_list, &vma_list_mutex, 0, true};

address_space *kernel_address_space()
{
    return &kernel_as;
}

// The address space the current thread runs in: a fork child's private AS, or
// AS0 (kernel_as) for the kernel and the non-fork initial application.  The
// allocation/query path (find_hole, allocate, evacuate, protect, ...) resolves
// its vma_list / vma_range_set through this, so a fork child's post-fork mmap
// lands in -- and its later faults resolve against -- the child's OWN address
// space.  For AS0 the accessors below alias the global vma_list / vma_range_set,
// so the heavily-tested non-fork path is unchanged.
address_space *current_address_space()
{
    auto t = sched::thread::current();
    if (t) {
        auto as = t->address_space();
        if (as) {
            return as;
        }
    }
    return &kernel_as;
}

// Per-AS accessors used by the allocation/query path.  In a CONF_fork build
// they resolve to the current thread's address space; AS0 aliases the globals.
static inline vma_list_type &cur_vma_list()
{
    return *current_address_space()->vmas;
}
static inline rwlock_t &cur_vma_list_mutex()
{
    return *current_address_space()->vmas_mutex;
}
static inline vma_range_set_type &cur_vma_range_set()
{
    return *current_address_space()->ranges;
}
static inline rwlock_t &cur_vma_range_set_mutex()
{
    return *current_address_space()->ranges_mutex;
}

// Arch hook: physical address of the kernel PML4 (CR3 value for AS0).
phys kernel_pt_root_phys();
// Arch hook: virtual pointer to the kernel PML4 (for cloning kernel slots).
pt_element<4> *kernel_pml4();

phys pt_root_phys(address_space *as)
{
    if (as->pt_root) {
        return as->pt_root;
    }
    // AS0 (or any AS with pt_root not yet cached): the arch kernel root.
    return kernel_pt_root_phys();
}

// The PML4 (virtual pointer) that the current thread's page-table walks should
// use.  Returns the child AS's private PML4 when the current thread runs in a
// child address space, else the kernel PML4 (AS0).  Called from get_root_pt().
pt_element<4> *current_pt_root()
{
    auto t = sched::thread::current();
    if (t) {
        auto as = t->address_space();
        if (as && as->top) {
            return as->top;
        }
    }
    return kernel_pml4();
}

// --- fork COW page-table clone --------------------------------------------
//
// Number of live fork-child address spaces.  A wait_record placed on a SHARED
// kernel condvar/mutex is a stack local; with per-child same-VA private stacks,
// that stack VA resolves to DIFFERENT physical pages in different address
// spaces, so a thread in one AS that walks the queue and dereferences another
// thread's stack-resident wait_record reads the wrong page.  When any child AS
// is live, ANY thread's wait_record can be dereferenced cross-AS (the parent's
// by a child, a child's by the parent), so wait_records must come from the
// identity-mapped kernel heap (same VA->phys in every AS) -- see
// fork_child_needs_heap_wait_record() and include/osv/wait_record.hh.
std::atomic<int> live_child_address_spaces{0};

// Recursively clone the child's copy of an application-range subtree of the
// parent's page table, level by level, marking most private 4K leaf pages
// copy-on-write.
//
// IMPORTANT (OSv has no user/kernel stack split): OSv runs kernel code on the
// SAME stack the application uses.  A context switch (switch_to) writes to that
// stack with interrupts disabled, and OSv forbids page faults in that context.
// So we must NOT copy-on-write-protect any *stack* page -- write-protecting the
// running thread's live stack would fault the very next stack push with irqs
// off (assert(preemptable()) in page_fault).  The child already runs on its
// own private copied stack (see fork_thread), so stack pages are simply shared
// writable.  MAP_SHARED pages are likewise shared writable (sharing preserved).
// These "share, don't COW" address ranges are passed in via cow_share_ranges.
struct cow_range { uintptr_t start, end; };
static const std::vector<cow_range> *cow_share_ranges;
// Ranges (the FORKING thread's live stack) that must be PRIVATIZED into the
// child: fresh private physical pages that byte-copy the parent's page, mapped
// at the SAME VA in the child, writable and NOT COW (OSv runs kernel code with
// irqs off on the app stack, so a COW write-fault there is illegal) and NOT
// shared (so the child owns its stack and frees it on teardown).  This is what
// lets the child resume on the parent's exact rsp/rbp (same-VA stack) while
// having private stack memory -- see arch/x64/fork.cc.
static const std::vector<cow_range> *cow_privatize_ranges;

static bool addr_is_shared(uintptr_t va)
{
    if (!cow_share_ranges) return false;
    for (auto &r : *cow_share_ranges) {
        if (va >= r.start && va < r.end) return true;
    }
    return false;
}

static bool addr_is_privatize(uintptr_t va)
{
    if (!cow_privatize_ranges) return false;
    for (auto &r : *cow_privatize_ranges) {
        if (va >= r.start && va < r.end) return true;
    }
    return false;
}

// The parent must hold vma_list_mutex for write while this runs.  base_virt is
// the virtual address that leaf entry 0 of this PT maps.
static void clone_pt_level0(pt_element<0> *parent_pt, pt_element<0> *child_pt,
                            uintptr_t base_virt)
{
    for (unsigned i = 0; i < pte_per_page; i++) {
        pt_element<0> ppte = parent_pt[i];
        if (ppte.empty()) {
            child_pt[i] = make_empty_pte<0>();
            continue;
        }
        uintptr_t va = base_virt + (uintptr_t)i * page_size;
        if (addr_is_privatize(va)) {
            // Forking thread's live stack: give the child its OWN physical
            // page (byte-copy of the parent's), mapped at the SAME VA, plain
            // writable (no COW bit, no shared bit).  The parent's PTE is left
            // untouched -- only the child diverges -- so the parent keeps
            // writing its own stack with irqs off and never faults, and the
            // child owns/frees this page on address-space teardown.
            void *child_page = memory::alloc_page();
            memcpy(child_page, phys_to_virt(ppte.addr()), page_size);
            pt_element<0> pv = ppte;
            if (pte_is_cow(pv)) {
                pv = pte_mark_cow(pv, false);
            }
            pv.set_writable(true);
            pv.set_addr(virt_to_phys(child_page), false);
            child_pt[i] = pv;
        } else if (addr_is_shared(va)) {
            // Stack / MAP_SHARED page: must stay genuinely shared + writable in
            // both parent and child (never COW).  Clear any COW bit and grant
            // write so both sides see each other's writes to the same phys
            // page.  Tag the child's copy pte_shared so teardown does not free
            // the jointly-owned physical frame.
            pt_element<0> sh = ppte;
            if (pte_is_cow(sh)) {
                sh = pte_mark_cow(sh, false);
            }
            sh.set_writable(true);
            parent_pt[i] = sh;
            sh.set_sw_bit(pte_shared, true);
            child_pt[i] = sh;
        } else if (ppte.writable() && !pte_is_cow(ppte)) {
            // Private writable page: make it COW (write-protect + cow bit) in
            // BOTH parent and child.
            pt_element<0> cow = pte_mark_cow(ppte, true);
            parent_pt[i] = cow;
            child_pt[i] = cow;
        } else {
            // Read-only or already-COW private page: share the same physical
            // page as-is (stays COW/read-only in the child too).
            child_pt[i] = ppte;
        }
    }
}

// clone_pt_level<N> for N in 1..2, carrying base_virt so leaf pages can be
// tested against the share-ranges.  (gnu++14: no if constexpr, so explicit
// specialization.)
template<int N> void split_large_page(hw_ptep<N> ptep); // defined below
template<> void split_large_page(hw_ptep<1> ptep);      // defined below
template<int N>
static void clone_pt_level(pt_element<N> *parent_pt, pt_element<N> *child_pt,
                           uintptr_t base_virt);

template<>
void clone_pt_level<1>(pt_element<1> *parent_pt, pt_element<1> *child_pt,
                       uintptr_t base_virt)
{
    // Level 1 (PD): each entry spans 2 MB.
    const uintptr_t step = (uintptr_t)page_size * pte_per_page; // 2 MB
    for (unsigned i = 0; i < pte_per_page; i++) {
        pt_element<1> ppte = parent_pt[i];
        if (ppte.empty()) { child_pt[i] = make_empty_pte<1>(); continue; }
        if (ppte.large()) {
            // A read-only 2 MB page can never diverge, so share it verbatim.
            // A WRITABLE 2 MB large page (large malloc, MAP_SHARED, large anon)
            // must NOT be shared as-is -- that lets a forked child's writes
            // leak into the parent.  Split it to 4 K in the PARENT in place,
            // then fall through to the normal 4 K clone path, which makes the
            // correct per-page decision (private-writable -> COW, MAP_SHARED ->
            // stays shared, read-only -> shared as-is).  Reuses the whole 4 K
            // COW machinery; cost is one 4 K page table per split large page.
            if (!ppte.writable()) { child_pt[i] = ppte; continue; }
            split_large_page(hw_ptep<1>::force(&parent_pt[i]));
            ppte = parent_pt[i]; // re-read: now a non-large intermediate pte
        }
        void *child_sub = memory::alloc_page();
        memset(child_sub, 0, page_size);
        auto parent_sub = phys_cast<pt_element<0>>(ppte.next_pt_addr());
        clone_pt_level0(parent_sub, static_cast<pt_element<0>*>(child_sub),
                        base_virt + (uintptr_t)i * step);
        pt_element<1> cpte = ppte;
        cpte.set_addr(virt_to_phys(child_sub), false);
        child_pt[i] = cpte;
    }
}

template<>
void clone_pt_level<2>(pt_element<2> *parent_pt, pt_element<2> *child_pt,
                       uintptr_t base_virt)
{
    // Level 2 (PDPT): each entry spans 1 GB.
    const uintptr_t step = (uintptr_t)page_size * pte_per_page * pte_per_page; // 1 GB
    for (unsigned i = 0; i < pte_per_page; i++) {
        pt_element<2> ppte = parent_pt[i];
        if (ppte.empty()) { child_pt[i] = make_empty_pte<2>(); continue; }
        if (ppte.large()) { child_pt[i] = ppte; continue; }
        void *child_sub = memory::alloc_page();
        memset(child_sub, 0, page_size);
        auto parent_sub = phys_cast<pt_element<1>>(ppte.next_pt_addr());
        clone_pt_level<1>(parent_sub, static_cast<pt_element<1>*>(child_sub),
                          base_virt + (uintptr_t)i * step);
        pt_element<2> cpte = ppte;
        cpte.set_addr(virt_to_phys(child_sub), false);
        child_pt[i] = cpte;
    }
}

address_space *clone_address_space(address_space *parent)
{
    // Force every allocation done while cloning (the child's anon_vma copies,
    // any std::vector growth) onto the identity kernel heap.  We hold the
    // parent's vmas_mutex for_write below; an arena allocation that faulted a
    // fresh arena page would re-enter the vma fault path and deadlock on that
    // same mutex (and give the child a COW-private copy of kernel vma
    // bookkeeping).  This is the malloc-during-fork recursion trap.
    fork_arena::kernel_heap_scope kh;
    // The parent's actual PML4 page (array of 512 level-3 entries).
    phys parent_pml4_phys = parent->top ? parent->top->next_pt_addr()
                                        : kernel_pt_root_phys();
    auto parent_pml4 = phys_cast<pt_element<3>>(parent_pml4_phys);

    // Allocate the child's PML4 page.
    void *child_pml4_page = memory::alloc_page();
    memset(child_pml4_page, 0, page_size);
    auto child_pml4 = static_cast<pt_element<3>*>(child_pml4_page);

    // Pre-reserve the child address_space object BEFORE taking vmas_mutex.
    // Allocating under the write-locked vmas_mutex is unsafe once the fork
    // arena is active: the identity malloc pool may need a refill that wants
    // to schedule the page-pool fill thread, but nothing can make progress
    // while this thread holds vmas_mutex for write -- a fork-time malloc
    // deadlock.  Doing every kernel allocation up front (pre-reserve) is the
    // documented cure for the malloc-during-fork trap.
    auto child = new address_space(virt_to_phys(child_pml4_page));

    // Snapshot of the parent's private VMAs, filled under the lock and turned
    // into the child's anon_vma copies AFTER the lock is released (so no malloc
    // runs under vmas_mutex).  Reserve generous capacity up front so the vector
    // never reallocates (mallocs) while the lock is held.
    struct vma_snap { uintptr_t start, end; unsigned perm, flags; };
    std::vector<vma_snap> snap;
    // Pre-grown share/privatize range vectors: filled under the lock but never
    // reallocated there (all growth malloc happens up front, before the lock).
    std::vector<cow_range> share_ranges;
    std::vector<cow_range> privatize_ranges;
    {
        size_t threads = sched::thread::numthreads();
        SCOPE_LOCK(parent->vmas_mutex->for_read());
        size_t n = 0;
        for (auto &v : *parent->vmas) { if (v.size()) n++; }
        snap.reserve(n + 8);
        // share_ranges holds MAP_SHARED/stack vmas + every live thread's stack;
        // reserve room for all vmas plus all threads so it never reallocates.
        share_ranges.reserve(n + threads + 16);
        privatize_ranges.reserve(4);
    }

    // Share every kernel PML4 slot (0 and 128..511) by copying the parent's
    // entry verbatim (points at the same lower-level tables).  Clone only the
    // application slots (1..127) so their PTEs can diverge under COW.
    PREVENT_STACK_PAGE_FAULT
    WITH_LOCK(parent->vmas_mutex->for_write()) {
        // Build the "share, don't COW" ranges: stack VMAs (OSv runs kernel code
        // on the app stack, so its pages must stay writable -- see the note on
        // clone_pt_level0) and MAP_SHARED VMAs (sharing must be preserved).
        for (auto &v : *parent->vmas) {
            if (v.size() == 0) continue;
            if ((v.flags() & mmap_stack) || (v.flags() & mmap_shared)) {
                share_ranges.push_back({v.start(), v.end()});
            }
        }
        // Always share the forking thread's live stack: OSv runs kernel code
        // (incl. the context switch, with irqs off) on it, so it must never be
        // write-protected.  get_stack_info() gives the current thread's stack.
        {
            // The FORKING thread's live stack is PRIVATIZED (not shared): the
            // child gets private byte-copies at the same VA so it can resume on
            // the parent's exact rsp/rbp with independent stack memory.  It
            // must stay plain-writable (no COW) since OSv runs kernel code with
            // irqs off on it -- a COW fault there is illegal.
            auto si = sched::thread::current()->get_stack_info();
            uintptr_t s = reinterpret_cast<uintptr_t>(si.begin);
            privatize_ranges.push_back({s, s + si.size});
        }
        // Likewise every OTHER live thread's stack: those threads keep running
        // in the PARENT address space and perform context switches (irqs off) on
        // their own stacks, which OSv forbids faulting on.  COW-protecting them
        // would fault the next switch.  The forked child is single-threaded and
        // never touches sibling stacks, so sharing them is safe.
        sched::with_all_threads([&share_ranges](sched::thread &t) {
            auto si = t.get_stack_info();
            if (si.begin) {
                uintptr_t s = reinterpret_cast<uintptr_t>(si.begin);
                share_ranges.push_back({s, s + si.size});
            }
        });
        cow_share_ranges = &share_ranges;
        cow_privatize_ranges = &privatize_ranges;

        for (unsigned slot = 0; slot < pte_per_page; slot++) {
            if (slot >= pml4_app_first && slot <= pml4_app_last) {
                pt_element<3> pslot = parent_pml4[slot];
                if (pslot.empty() || pslot.large()) {
                    // Empty, or (unexpected) large: share as-is / leave empty.
                    child_pml4[slot] = pslot.empty() ? make_empty_pte<3>() : pslot;
                    continue;
                }
                // Deep-copy this app subtree (level 2 downwards) into the
                // child, marking private 4K leaves COW.
                void *child_sub = memory::alloc_page();
                memset(child_sub, 0, page_size);
                auto parent_sub = phys_cast<pt_element<2>>(pslot.next_pt_addr());
                clone_pt_level<2>(parent_sub, static_cast<pt_element<2>*>(child_sub),
                                  (uintptr_t)slot << 39);
                pt_element<3> cslot = pslot;
                cslot.set_addr(virt_to_phys(child_sub), false);
                child_pml4[slot] = cslot;
            } else {
                // Kernel slot: share verbatim.
                child_pml4[slot] = parent_pml4[slot];
            }
        }
        cow_share_ranges = nullptr;
        cow_privatize_ranges = nullptr;

        // Snapshot the parent's private VMAs into `snap` while still under the
        // lock (reads only; the pre-reserved vector storage is identity-mapped
        // so its push_back never faults).  The child's anon_vma copies are
        // built AFTER the lock is released.  We must NOT do any write that can
        // COW-fault (e.g. to a kernel .bss global we just write-protected)
        // while holding vmas_mutex for write: the COW fault handler re-acquires
        // vmas_mutex, which self-deadlocks.  So flush_tlb_all() and the
        // live_child_address_spaces bump are deferred until after the lock.
        for (auto &v : *parent->vmas) {
            if (v.size() == 0) {
                continue;
            }
            snap.push_back({v.start(), v.end(), v.perm(), v.flags()});
        }
    } // release vmas_mutex

    // Now that vmas_mutex is released, writes that may hit a copy-on-write
    // kernel .bss page (the atomic bump; the TLB-flush bookkeeping globals) can
    // fault and be serviced normally.  Doing them under the write lock would
    // deadlock (COW fault -> vm_fault -> vmas_mutex for_write, already held).
    live_child_address_spaces.fetch_add(1, std::memory_order_relaxed);
    // Make the write-protection we applied to the parent's page tables visible
    // on all CPUs before the parent continues.
    mmu::flush_tlb_all();

    // Build the child's vma_list from the snapshot -- these new anon_vma
    // allocations are free to hit the malloc pool refill path without risking a
    // fork-time deadlock.  Each vma also goes into the child's OWN range set
    // (its edge markers were inserted by the child's vma_list ctor); the
    // allocation path (find_hole/allocate/evacuate) consults child->ranges.
    for (auto &s : snap) {
        auto *nv = new anon_vma(addr_range(s.start, s.end), s.perm, s.flags);
        child->vmas->insert(*nv);
        WITH_LOCK(child->ranges_mutex->for_write()) {
            child->ranges->insert(vma_range(nv));
        }
    }
    return child;
}

// --- fork COW page-table teardown ------------------------------------------
//
// Recursively free a child's private app-range subtree.  Intermediate tables
// are always freed (they were allocated fresh for the child).  Leaf 4K pages
// are freed only if they are the child's PRIVATE copy (writable, non-COW);
// COW / read-only leaves are shared with the parent and left intact.
static void free_child_pt_level0(pt_element<0> *pt)
{
    for (unsigned i = 0; i < pte_per_page; i++) {
        pt_element<0> e = pt[i];
        if (e.empty()) continue;
        if (e.writable() && !pte_is_cow(e) && !e.sw_bit(pte_shared)) {
            // Child's own COW-copied private page: free it.  (pte_shared pages
            // are jointly-owned stack/MAP_SHARED frames -- never free them.)
            memory::free_page(phys_to_virt(e.addr()));
        }
        // else: shared (COW, read-only, or pte_shared) -- leave the frame.
    }
    memory::free_page(pt);
}

template<int N>
static void free_child_pt_level(pt_element<N> *pt);

template<>
void free_child_pt_level<1>(pt_element<1> *pt)
{
    for (unsigned i = 0; i < pte_per_page; i++) {
        pt_element<1> e = pt[i];
        if (e.empty() || e.large()) continue;   // large: shared mapping, skip
        free_child_pt_level0(phys_cast<pt_element<0>>(e.next_pt_addr()));
    }
    memory::free_page(pt);
}
template<>
void free_child_pt_level<2>(pt_element<2> *pt)
{
    for (unsigned i = 0; i < pte_per_page; i++) {
        pt_element<2> e = pt[i];
        if (e.empty() || e.large()) continue;
        free_child_pt_level<1>(phys_cast<pt_element<1>>(e.next_pt_addr()));
    }
    memory::free_page(pt);
}

void destroy_address_space(address_space *as)
{
    if (!as || as == &kernel_as) {
        return;
    }
    // Free the child's private page tables for the application slots (1..127)
    // and the PML4 page.  Kernel slots (0, 128..511) are shared and must NOT
    // be freed.  For leaf 4K pages: free only the child's PRIVATE copies
    // (writable, non-COW) -- COW/read-only leaves are still shared with the
    // parent and must be left alone.
    phys pml4_phys = as->top ? as->top->next_pt_addr() : 0;
    if (pml4_phys) {
        auto pml4 = phys_cast<pt_element<3>>(pml4_phys);
        for (unsigned slot = pml4_app_first; slot <= pml4_app_last; slot++) {
            pt_element<3> e3 = pml4[slot];
            if (e3.empty() || e3.large()) continue;
            free_child_pt_level<2>(phys_cast<pt_element<2>>(e3.next_pt_addr()));
        }
        memory::free_page(phys_to_virt(pml4_phys));
    }
    delete as;
    live_child_address_spaces.fetch_sub(1, std::memory_order_relaxed);
}
#else // !CONF_fork
// Non-fork build: the allocation/query path uses the single global vma_list /
// vma_range_set directly.  These accessors make the shared code below identical
// in shape to the CONF_fork build while compiling to the exact same global
// references (byte-identical non-fork behaviour).
static inline vma_list_type &cur_vma_list() { return vma_list; }
static inline rwlock_t &cur_vma_list_mutex() { return vma_list_mutex; }
static inline vma_range_set_type &cur_vma_range_set() { return vma_range_set; }
static inline rwlock_t &cur_vma_range_set_mutex() { return vma_range_set_mutex; }
#endif // CONF_fork

// A mutex serializing modifications to the high part of the page table
// (linear map, etc.) which are not part of vma_list.
mutex page_table_high_mutex;

// 1's for the bits provided by the pte for this level
// 0's for the bits provided by the virtual address for this level
phys pte_level_mask(unsigned level)
{
    auto shift = level * ilog2_roundup_constexpr(pte_per_page)
        + ilog2_roundup_constexpr(page_size);
    return ~((phys(1) << shift) - 1);
}

#ifdef __x86_64__
static void *elf_phys_start = (void*)OSV_KERNEL_BASE;
#endif

#ifdef __aarch64__
void *elf_phys_start;
extern "C" u64 kernel_vm_shift;
#endif

void* phys_to_virt(phys pa)
{
    void* phys_addr = reinterpret_cast<void*>(pa);
    if ((phys_addr >= elf_phys_start) && (phys_addr < elf_phys_start + elf_size)) {
#ifdef __x86_64__
        return (void*)(phys_addr + OSV_KERNEL_VM_SHIFT);
#endif
#ifdef __aarch64__
        return (void*)(phys_addr + kernel_vm_shift);
#endif
    }

    return phys_mem + pa;
}

phys virt_to_phys_pt(void* virt);

phys virt_to_phys(void *virt)
{
    if ((virt >= elf_start) && (virt < elf_start + elf_size)) {
#ifdef __x86_64__
        return reinterpret_cast<phys>((void*)(virt - OSV_KERNEL_VM_SHIFT));
#endif
#ifdef __aarch64__
        return reinterpret_cast<phys>((void*)(virt - kernel_vm_shift));
#endif
    }

#if CONF_memory_debug
    if (virt > debug_base) {
        return virt_to_phys_pt(virt);
    }
#endif

    // For VMA-mapped addresses (below phys_mem), walk the page table.
    // This handles e.g. malloc_large fallback allocations used as DMA
    // buffers when physical memory is fragmented.
    if (reinterpret_cast<uintptr_t>(virt) < reinterpret_cast<uintptr_t>(phys_mem)) {
        return virt_to_phys_pt(virt);
    }
    return reinterpret_cast<uintptr_t>(virt) & (mem_area_size - 1);
}

template <int N, typename MakePTE>
phys allocate_intermediate_level(MakePTE make_pte)
{
    phys pt_page = virt_to_phys(memory::alloc_page());
    // since the pt is not yet mapped, we don't need to use hw_ptep
    pt_element<N>* pt = phys_cast<pt_element<N>>(pt_page);
    for (auto i = 0; i < pte_per_page; ++i) {
        pt[i] = make_pte(i);
    }
    return pt_page;
}

template<int N>
void allocate_intermediate_level(hw_ptep<N> ptep, pt_element<N> org)
{
    phys pt_page = allocate_intermediate_level<N>([org](int i) {
        auto tmp = org;
        phys addend = phys(i) << page_size_shift;
        tmp.set_addr(tmp.addr() | addend, false);
        return tmp;
    });
    ptep.write(make_intermediate_pte(ptep, pt_page));
}

template<int N>
void allocate_intermediate_level(hw_ptep<N> ptep)
{
    phys pt_page = allocate_intermediate_level<N>([](int i) {
        return make_empty_pte<N>();
    });
    if (!ptep.compare_exchange(make_empty_pte<N>(), make_intermediate_pte(ptep, pt_page))) {
        memory::free_page(phys_to_virt(pt_page));
    }
}

// only 4k can be cow for now
pt_element<0> pte_mark_cow(pt_element<0> pte, bool cow)
{
    if (cow) {
        pte.set_writable(false);
    }
    pte.set_sw_bit(pte_cow, cow);
    return pte;
}

template<int N>
bool change_perm(hw_ptep<N> ptep, unsigned int perm)
{
    static_assert(pt_level_traits<N>::leaf_capable::value, "non leaf pte");
    pt_element<N> pte = ptep.read();
    unsigned int old = (pte.valid() ? perm_read : 0) |
        (pte.writable() ? perm_write : 0) |
        (pte.executable() ? perm_exec : 0);

    if (pte_is_cow(pte)) {
        perm &= ~perm_write;
    }

    // Note: in x86, if the present bit (0x1) is off, not only read is
    // disallowed, but also write and exec. So in mprotect, if any
    // permission is requested, we must also grant read permission.
    // Linux does this too.
    pte.set_valid(true);
    pte.set_writable(perm & perm_write);
    pte.set_executable(perm & perm_exec);
    pte.set_rsvd_bit(0, !perm);
    ptep.write(pte);

#ifdef __x86_64__
    return old & ~perm;
#endif
#ifdef __aarch64__
    //TODO: This will trigger full tlb flush in slightly more cases than on x64
    //and in future we should investigate more precise and hopefully lighter
    //mechanism. But for now it will do it.
    return old != perm;
#endif
}

template<int N>
void split_large_page(hw_ptep<N> ptep)
{
}

template<>
void split_large_page(hw_ptep<1> ptep)
{
    pt_element<1> pte_orig = ptep.read();
    pte_orig.set_large(false);
    allocate_intermediate_level(ptep, pte_orig);
}

struct page_allocator {
    virtual bool map(uintptr_t offset, hw_ptep<0> ptep, pt_element<0> pte, bool write) = 0;
    virtual bool map(uintptr_t offset, hw_ptep<1> ptep, pt_element<1> pte, bool write) = 0;
    virtual bool unmap(void *addr, uintptr_t offset, hw_ptep<0> ptep) = 0;
    virtual bool unmap(void *addr, uintptr_t offset, hw_ptep<1> ptep) = 0;
    virtual ~page_allocator() {}
};

unsigned long all_vmas_size()
{
    SCOPE_LOCK(vma_list_mutex.for_read());
    return std::accumulate(vma_list.begin(), vma_list.end(), size_t(0), [](size_t s, vma& v) { return s + v.size(); });
}

void clamp(uintptr_t& vstart1, uintptr_t& vend1,
           uintptr_t min, size_t max, size_t slop)
{
    vstart1 &= ~(slop - 1);
    vend1 |= (slop - 1);
    vstart1 = std::max(vstart1, min);
    vend1 = std::min(vend1, max);
}

constexpr unsigned pt_index(uintptr_t virt, unsigned level)
{
    return pt_index(reinterpret_cast<void*>(virt), level);
}

unsigned nr_page_sizes = 2; // FIXME: detect 1GB pages

void set_nr_page_sizes(unsigned nr)
{
    nr_page_sizes = nr;
}

enum class allocate_intermediate_opt : bool {no = true, yes = false};
enum class skip_empty_opt : bool {no = true, yes = false};
enum class descend_opt : bool {no = true, yes = false};
enum class once_opt : bool {no = true, yes = false};
enum class split_opt : bool {no = true, yes = false};
enum class account_opt: bool {no = true, yes = false};

// Parameter descriptions:
//  Allocate - if "yes" page walker will allocate intermediate page if one is missing
//             otherwise it will skip to next address.
//  Skip     - if "yes" page walker will not call leaf page handler on an empty pte.
//  Descend  - if "yes" page walker will descend one level if large page range is mapped
//             by small pages, otherwise it will call huge_page() on intermediate small pte
//  Once     - if "yes" page walker will not loop over range of pages
//  Split    - If "yes" page walker will split huge pages to small pages while walking
template<allocate_intermediate_opt Allocate, skip_empty_opt Skip = skip_empty_opt::yes,
        descend_opt Descend = descend_opt::yes, once_opt Once = once_opt::no, split_opt Split = split_opt::yes>
class page_table_operation {
protected:
    template<typename T>  bool opt2bool(T v) { return v == T::yes; }
public:
    bool allocate_intermediate(void) { return opt2bool(Allocate); }
    bool skip_empty(void) { return opt2bool(Skip); }
    bool descend(void) { return opt2bool(Descend); }
    bool once(void) { return opt2bool(Once); }
    template<int N>
    bool split_large(hw_ptep<N> ptep, int level) { return opt2bool(Split); }
    unsigned nr_page_sizes(void) { return mmu::nr_page_sizes; }

    template<int N>
    pt_element<N> ptep_read(hw_ptep<N> ptep) { return ptep.read(); }

    // page() function is called on leaf ptes. Each page table operation
    // have to provide its own version.
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) { assert(0); }
    // if huge page range is covered by smaller pages some page table operations
    // may want to have special handling for level 1 non leaf pte. intermediate_page_pre()
    // is called just before descending into the next level, while intermediate_page_post()
    // is called just after.
    void intermediate_page_pre(hw_ptep<1> ptep, uintptr_t offset) {}
    void intermediate_page_post(hw_ptep<1> ptep, uintptr_t offset) {}
    // Page walker calls page() when it a whole leaf page need to be handled, but if it
    // has 2M pte and less then 2M of virt memory to operate upon and split is disabled
    // sup_page is called instead. So if you are here it means that page walker encountered
    // 2M pte and page table operation wants to do something special with sub-region of it
    // since it disabled splitting.
    void sub_page(hw_ptep<1> ptep, int level, uintptr_t offset) { return; }
};

template<typename PageOps, int N>
static inline typename std::enable_if<pt_level_traits<N>::large_capable::value>::type
sub_page(PageOps& pops, hw_ptep<N> ptep, int level, uintptr_t offset)
{
    pops.sub_page(ptep, level, offset);
}

template<typename PageOps, int N>
static inline typename std::enable_if<!pt_level_traits<N>::large_capable::value>::type
sub_page(PageOps& pops, hw_ptep<N> ptep, int level, uintptr_t offset)
{
}

template<typename PageOps, int N>
static inline typename std::enable_if<pt_level_traits<N>::leaf_capable::value, bool>::type
page(PageOps& pops, hw_ptep<N> ptep, uintptr_t offset)
{
    return pops.page(ptep, offset);
}

template<typename PageOps, int N>
static inline typename std::enable_if<!pt_level_traits<N>::leaf_capable::value, bool>::type
page(PageOps& pops, hw_ptep<N> ptep, uintptr_t offset)
{
    assert(0);
    return false;
}

template<typename PageOps, int N>
static inline typename std::enable_if<pt_level_traits<N>::large_capable::value>::type
intermediate_page_pre(PageOps& pops, hw_ptep<N> ptep, uintptr_t offset)
{
    pops.intermediate_page_pre(ptep, offset);
}

template<typename PageOps, int N>
static inline typename std::enable_if<!pt_level_traits<N>::large_capable::value>::type
intermediate_page_pre(PageOps& pops, hw_ptep<N> ptep, uintptr_t offset)
{
}

template<typename PageOps, int N>
static inline typename std::enable_if<pt_level_traits<N>::large_capable::value>::type
intermediate_page_post(PageOps& pops, hw_ptep<N> ptep, uintptr_t offset)
{
    pops.intermediate_page_post(ptep, offset);
}

template<typename PageOps, int N>
static inline typename std::enable_if<!pt_level_traits<N>::large_capable::value>::type
intermediate_page_post(PageOps& pops, hw_ptep<N> ptep, uintptr_t offset)
{
}

template<typename PageOp, int ParentLevel> class map_level;

template<typename PageOp>
        void map_range(uintptr_t vma_start, uintptr_t vstart, size_t size, PageOp& page_mapper, size_t slop = page_size)
{
    map_level<PageOp, 4> pt_mapper(vma_start, vstart, size, page_mapper, slop);
    pt_mapper(hw_ptep<4>::force(mmu::get_root_pt(vstart)));
    // On some architectures with weak memory model it is necessary
    // to force writes to page table entries complete and instruction pipeline
    // flushed so that new mappings are properly visible when relevant newly mapped
    // virtual memory areas are accessed right after this point.
    // So let us call arch-specific function to execute the logic above if
    // applicable for given architecture.
    synchronize_page_table_modifications();
}

template<typename PageOp, int ParentLevel> class map_level {
private:
    uintptr_t vma_start;
    uintptr_t vcur;
    uintptr_t vend;
    size_t slop;
    PageOp& page_mapper;
    static constexpr int level = ParentLevel - 1;

    friend void map_range<PageOp>(uintptr_t, uintptr_t, size_t, PageOp&, size_t);
    friend class map_level<PageOp, ParentLevel + 1>;

    map_level(uintptr_t vma_start, uintptr_t vcur, size_t size, PageOp& page_mapper, size_t slop) :
        vma_start(vma_start), vcur(vcur), vend(vcur + size - 1), slop(slop), page_mapper(page_mapper) {}
    pt_element<ParentLevel> read(const hw_ptep<ParentLevel>& ptep) const {
        return page_mapper.ptep_read(ptep);
    }
    pt_element<level> read(const hw_ptep<level>& ptep) const {
        return page_mapper.ptep_read(ptep);
    }
    hw_ptep<level> follow(hw_ptep<ParentLevel> ptep)
    {
        return hw_ptep<level>::force(phys_cast<pt_element<level>>(read(ptep).next_pt_addr()));
    }
    bool skip_pte(hw_ptep<level> ptep) {
        return page_mapper.skip_empty() && read(ptep).empty();
    }
    bool descend(hw_ptep<level> ptep) {
        return page_mapper.descend() && !read(ptep).empty() && !read(ptep).large();
    }
    template<int N>
    typename std::enable_if<N == 0>::type
    map_range(uintptr_t vcur, size_t size, PageOp& page_mapper, size_t slop,
            hw_ptep<N> ptep, uintptr_t base_virt)
    {
    }
    template<int N>
    typename std::enable_if<N == level && N != 0>::type
    map_range(uintptr_t vcur, size_t size, PageOp& page_mapper, size_t slop,
            hw_ptep<N> ptep, uintptr_t base_virt)
    {
        map_level<PageOp, level> pt_mapper(vma_start, vcur, size, page_mapper, slop);
        pt_mapper(ptep, base_virt);
    }
    void operator()(hw_ptep<ParentLevel> parent, uintptr_t base_virt = 0) {
        if (!read(parent).valid()) {
            if (!page_mapper.allocate_intermediate()) {
                return;
            }
            allocate_intermediate_level(parent);
        } else if (read(parent).large()) {
            if (page_mapper.split_large(parent, ParentLevel)) {
                // We're trying to change a small page out of a huge page (or
                // in the future, potentially also 2 MB page out of a 1 GB),
                // so we need to first split the large page into smaller pages.
                // Our implementation ensures that it is ok to free pieces of a
                // alloc_huge_page() with free_page(), so it is safe to do such a
                // split.
                split_large_page(parent);
            } else {
                // If page_mapper does not want to split, let it handle subpage by itself
                sub_page(page_mapper, parent, ParentLevel, base_virt - vma_start);
                return;
            }
        }
        auto pt = follow(parent);
        phys step = phys(1) << (page_size_shift + level * pte_per_page_shift);
        auto idx = pt_index(vcur, level);
        auto eidx = pt_index(vend, level);
        base_virt += idx * step;
        base_virt = (int64_t(base_virt) << 16) >> 16; // extend 47th bit

        do {
            auto ptep = pt.at(idx);
            uintptr_t vstart1 = vcur, vend1 = vend;
            clamp(vstart1, vend1, base_virt, base_virt + step - 1, slop);
            if (unsigned(level) < page_mapper.nr_page_sizes() && vstart1 == base_virt && vend1 == base_virt + step - 1) {
                uintptr_t offset = base_virt - vma_start;
                if (level) {
                    if (!skip_pte(ptep)) {
                        if (descend(ptep) || !page(page_mapper, ptep, offset)) {
                            intermediate_page_pre(page_mapper, ptep, offset);
                            map_range(vstart1, vend1 - vstart1 + 1, page_mapper, slop, ptep, base_virt);
                            intermediate_page_post(page_mapper, ptep, offset);
                        }
                    }
                } else {
                    if (!skip_pte(ptep)) {
                        page(page_mapper, ptep, offset);
                    }
                }
            } else {
                map_range(vstart1, vend1 - vstart1 + 1, page_mapper, slop, ptep, base_virt);
            }
            base_virt += step;
            ++idx;
        } while(!page_mapper.once() && idx <= eidx);
    }
};

class linear_page_mapper :
        public page_table_operation<allocate_intermediate_opt::yes, skip_empty_opt::no, descend_opt::no> {
    phys start;
    phys end;
    mattr mem_attr;
public:
    linear_page_mapper(phys start, size_t size, mattr mem_attr = mattr_default) :
        start(start), end(start + size), mem_attr(mem_attr) {}
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        phys addr = start + offset;
        assert(addr < end);
        ptep.write(make_leaf_pte(ptep, addr, mmu::perm_rwx, mem_attr));
        return true;
    }
};

template<allocate_intermediate_opt Allocate, skip_empty_opt Skip = skip_empty_opt::yes,
         account_opt Account = account_opt::no>
class vma_operation :
        public page_table_operation<Allocate, Skip, descend_opt::yes, once_opt::no, split_opt::yes> {
public:
    // returns true if tlb flush is needed after address range processing is completed.
    bool tlb_flush_needed(void) { return false; }
    // this function is called at the very end of operate_range(). vma_operation may do
    // whatever cleanup is needed here.
    void finalize(void) { return; }

    ulong account_results(void) { return _total_operated; }
    void account(size_t size) { if (this->opt2bool(Account)) _total_operated += size; }
private:
    // We don't need locking because each walk will create its own instance, so
    // while two instances can operate over the same linear address (therefore
    // all the cmpxcghs), the same instance will go linearly over its duty.
    ulong _total_operated = 0;
};

/*
 * populate() populates the page table with the entries it is (assumed to be)
 * missing to span the given virtual-memory address range, and then pre-fills
 * (using the given fill function) these pages and sets their permissions to
 * the given ones. This is part of the mmap implementation.
 */
template <account_opt T = account_opt::no>
class populate : public vma_operation<allocate_intermediate_opt::yes, skip_empty_opt::no, T> {
private:
    page_allocator* _page_provider;
    unsigned int _perm;
    bool _write;
    bool _map_dirty;
    template<int N>
    bool skip(pt_element<N> pte) {
        if (pte.empty()) {
            return false;
        }
        return !_write || pte.writable();
    }
    template<int N>
    inline pt_element<N> dirty(pt_element<N> pte) {
        pte.set_dirty(_map_dirty || _write);
        return pte;
    }
public:
    populate(page_allocator* pops, unsigned int perm, bool write = false, bool map_dirty = true) :
        _page_provider(pops), _perm(perm), _write(write), _map_dirty(map_dirty) { }
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        auto pte = ptep.read();
        if (skip(pte)) {
            return true;
        }

        pte = dirty(make_leaf_pte(ptep, 0, _perm));

        try {
            if (_page_provider->map(offset, ptep, pte, _write)) {
                this->account(pt_level_traits<N>::size::value);
            }
        } catch(std::exception&) {
            return false;
        }
        return true;
    }
};

template <account_opt Account = account_opt::no>
class populate_small : public populate<Account> {
public:
    populate_small(page_allocator* pops, unsigned int perm, bool write = false, bool map_dirty = true) :
        populate<Account>(pops, perm, write, map_dirty) { }
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        assert(!pt_level_traits<N>::large_capable::value);
        return populate<Account>::page(ptep, offset);
    }
    unsigned nr_page_sizes(void) { return 1; }
};

class splithugepages : public vma_operation<allocate_intermediate_opt::no, skip_empty_opt::yes, account_opt::no> {
public:
    splithugepages() { }
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset)
    {
        assert(!pt_level_traits<N>::large_capable::value);
        return true;
    }
    unsigned nr_page_sizes(void) { return 1; }
};

struct tlb_gather {
    static constexpr size_t max_pages = 20;
    struct tlb_page {
        void* addr;
        size_t size;
    };
    size_t nr_pages = 0;
    tlb_page pages[max_pages];
    bool push(void* addr, size_t size) {
        bool flushed = false;
        if (nr_pages == max_pages) {
            flush();
            flushed = true;
        }
        pages[nr_pages++] = { addr, size };
        return flushed;
    }
    bool flush() {
        if (!nr_pages) {
            return false;
        }
        mmu::flush_tlb_all();
        for (auto i = 0u; i < nr_pages; ++i) {
            auto&& tp = pages[i];
            if (tp.size == page_size) {
                memory::free_page(tp.addr);
            } else {
                memory::free_huge_page(tp.addr, tp.size);
            }
        }
        nr_pages = 0;
        return true;
    }
};

/*
 * Undo the operation of populate(), freeing memory allocated by populate()
 * and marking the pages non-present.
 */
template <account_opt T = account_opt::no>
class unpopulate : public vma_operation<allocate_intermediate_opt::no, skip_empty_opt::yes, T> {
private:
    tlb_gather _tlb_gather;
    page_allocator* _pops;
    bool do_flush = false;
public:
    unpopulate(page_allocator* pops) : _pops(pops) {}
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        void* addr = phys_to_virt(ptep.read().addr());
        size_t size = pt_level_traits<N>::size::value;
        // Note: we free the page even if it is already marked "not present".
        // evacuate() makes sure we are only called for allocated pages, and
        // not-present may only mean mprotect(PROT_NONE).
        if (_pops->unmap(addr, offset, ptep)) {
            do_flush = !_tlb_gather.push(addr, size);
        } else {
            do_flush = true;
        }
        this->account(size);
        return true;
    }
    void intermediate_page_post(hw_ptep<1> ptep, uintptr_t offset) {
        osv::rcu_defer([](void *page) { memory::free_page(page); }, phys_to_virt(ptep.read().addr()));
        ptep.write(make_empty_pte<1>());
    }
    bool tlb_flush_needed(void) {
        return !_tlb_gather.flush() && do_flush;
    }
    void finalize(void) {}
};

class protection : public vma_operation<allocate_intermediate_opt::no, skip_empty_opt::yes> {
private:
    unsigned int perm;
    bool do_flush;
public:
    protection(unsigned int perm) : perm(perm), do_flush(false) { }
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        do_flush |= change_perm(ptep, perm);
        return true;
    }
    bool tlb_flush_needed(void) {return do_flush;}
};

template <typename T, account_opt Account = account_opt::no>
class dirty_cleaner : public vma_operation<allocate_intermediate_opt::no, skip_empty_opt::yes, Account> {
private:
    bool do_flush;
    T handler;
public:
    dirty_cleaner(T handler) : do_flush(false), handler(handler) {}

    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        pt_element<N> pte = ptep.read();
        if (!pte.dirty()) {
            return true;
        }
        do_flush |= true;
        pte.set_dirty(false);
        ptep.write(pte);
        handler(ptep.read().addr(), offset, pt_level_traits<N>::size::value);
        return true;
    }

    bool tlb_flush_needed(void) {return do_flush;}
    void finalize() {
        handler.finalize();
    }
};

class dirty_page_sync {
    friend dirty_cleaner<dirty_page_sync, account_opt::yes>;
    friend file_vma;
private:
    file *_file;
    f_offset _offset;
    uint64_t _size;
    struct elm {
        iovec iov;
        off_t offset;
    };
    std::stack<elm> queue;
    dirty_page_sync(file *file, f_offset offset, uint64_t size) : _file(file), _offset(offset), _size(size) {}
    void operator()(phys addr, uintptr_t offset, size_t size) {
        off_t off = _offset + offset;
        size_t len = std::min(size, _size - off);
        queue.push(elm{{phys_to_virt(addr), len}, off});
    }
    void finalize() {
        while(!queue.empty()) {
            elm w = queue.top();
            uio data{&w.iov, 1, w.offset, ssize_t(w.iov.iov_len), UIO_WRITE};
            int error = _file->write(&data, FOF_OFFSET);
            if (error) {
                throw make_error(error);
            }
            queue.pop();
        }
    }
};

class virt_to_phys_map :
        public page_table_operation<allocate_intermediate_opt::no, skip_empty_opt::yes,
        descend_opt::yes, once_opt::yes, split_opt::no> {
private:
    uintptr_t v;
    phys result;
    static constexpr phys null = ~0ull;
    virt_to_phys_map(uintptr_t v) : v(v), result(null) {}

    phys addr(void) {
        assert(result != null);
        return result;
    }
public:
    friend phys virt_to_phys_pt(void* virt);
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        assert(result == null);
        result = ptep.read().addr() | (v & ~pte_level_mask(N));
        return true;
    }
    void sub_page(hw_ptep<1> ptep, int l, uintptr_t offset) {
        assert(ptep.read().large());
        page(ptep, offset);
    }
};

class cleanup_intermediate_pages
    : public page_table_operation<
          allocate_intermediate_opt::no,
          skip_empty_opt::yes,
          descend_opt::yes,
          once_opt::no,
          split_opt::no> {
public:
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        if (!pt_level_traits<N>::large_capable::value) {
            ++live_ptes;
        }
        return true;
    }
    void intermediate_page_pre(hw_ptep<1> ptep, uintptr_t offset) {
        live_ptes = 0;
    }
    void intermediate_page_post(hw_ptep<1> ptep, uintptr_t offset) {
        if (!live_ptes) {
            auto old = ptep.read();
            auto v = phys_cast<u64*>(old.addr());
            for (unsigned i = 0; i < 512; ++i) {
                assert(v[i] == 0);
            }
            ptep.write(make_empty_pte<1>());
            osv::rcu_defer([](void *page) { memory::free_page(page); }, phys_to_virt(old.addr()));
            do_flush = true;
        }
    }
    bool tlb_flush_needed() { return do_flush; }
    void finalize() {}
    ulong account_results(void) { return 0; }
private:
    unsigned live_ptes;
    bool do_flush = false;
};

class virt_to_pte_map_rcu :
        public page_table_operation<allocate_intermediate_opt::no, skip_empty_opt::yes,
        descend_opt::yes, once_opt::yes, split_opt::no> {
private:
    virt_pte_visitor& _visitor;
    virt_to_pte_map_rcu(virt_pte_visitor& visitor) : _visitor(visitor) {}

public:
    friend void virt_visit_pte_rcu(uintptr_t, virt_pte_visitor&);
    template<int N>
    pt_element<N> ptep_read(hw_ptep<N> ptep) {
        return ptep.ll_read();
    }
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        auto pte = ptep_read(ptep);
        _visitor.pte(pte);
        assert(pt_level_traits<N>::large_capable::value == pte.large());
        return true;
    }
    void sub_page(hw_ptep<1> ptep, int l, uintptr_t offset) {
        page(ptep, offset);
    }
};

template<typename T> ulong operate_range(T mapper, void *vma_start, void *start, size_t size)
{
    start = align_down(start, page_size);
    size = std::max(align_up(size, page_size), page_size);
    uintptr_t virt = reinterpret_cast<uintptr_t>(start);
    map_range(reinterpret_cast<uintptr_t>(vma_start), virt, size, mapper);

    // TODO: consider if instead of requesting a full TLB flush, we should
    // instead try to make more judicious use of INVLPG - e.g., in
    // split_large_page() and other specific places where we modify specific
    // page table entries.
    if (mapper.tlb_flush_needed()) {
        mmu::flush_tlb_all();
    }
    mapper.finalize();
    return mapper.account_results();
}

template<typename T> ulong operate_range(T mapper, void *start, size_t size)
{
    return operate_range(mapper, start, start, size);
}

phys virt_to_phys_pt(void* virt)
{
    auto v = reinterpret_cast<uintptr_t>(virt);
    auto vbase = align_down(v, page_size);
    virt_to_phys_map v2p_mapper(v);
    map_range(vbase, vbase, page_size, v2p_mapper);
    return v2p_mapper.addr();
}

void virt_visit_pte_rcu(uintptr_t virt, virt_pte_visitor& visitor)
{
    auto vbase = align_down(virt, page_size);
    virt_to_pte_map_rcu v2pte_mapper(visitor);
    WITH_LOCK(osv::rcu_read_lock) {
        map_range(vbase, vbase, page_size, v2pte_mapper);
    }
}

bool contains(uintptr_t start, uintptr_t end, vma& y)
{
    return y.start() >= start && y.end() <= end;
}

// So that we don't need to create a vma (with size, permission and alot of
// other irrelevant data) just to find an address in the vma list, we have
// the following addr_compare, which compares exactly like vma_compare does,
// except that it takes a bare uintptr_t instead of a vma.
class addr_compare {
public:
    bool operator()(const vma& x, uintptr_t y) const { return x.start() < y; }
    bool operator()(uintptr_t x, const vma& y) const { return x < y.start(); }
};

// Find the single (if any) vma which contains the given address.
// The complexity is logarithmic in the number of vmas in vma_list.
static inline vma_list_type::iterator
find_intersecting_vma_in(vma_list_type &vmas, uintptr_t addr) {
    auto vma = vmas.lower_bound(addr, addr_compare());
    if (vma->start() == addr) {
        return vma;
    }
    // Otherwise, vma->start() > addr, so we need to check the previous vma
    --vma;
    if (addr >= vma->start() && addr < vma->end()) {
        return vma;
    } else {
        return vmas.end();
    }
}

static inline vma_list_type::iterator
find_intersecting_vma(uintptr_t addr) {
    return find_intersecting_vma_in(cur_vma_list(), addr);
}

// Find the list of vmas which intersect a given address range. Because the
// vmas are sorted in vma_list, the result is a consecutive slice of vma_list,
// [first, second), between the first returned iterator (inclusive), and the
// second returned iterator (not inclusive).
// The complexity is logarithmic in the number of vmas in vma_list.
static inline std::pair<vma_list_type::iterator, vma_list_type::iterator>
find_intersecting_vmas(const addr_range& r)
{
    vma_list_type &vmas = cur_vma_list();
    if (r.end() <= r.start()) { // empty range, so nothing matches
        return {vmas.end(), vmas.end()};
    }
    auto start = vmas.lower_bound(r.start(), addr_compare());
    if (start->start() > r.start()) {
        // The previous vma might also intersect with our range if it ends
        // after our range's start.
        auto prev = std::prev(start);
        if (prev->end() > r.start()) {
            start = prev;
        }
    }
    // If the start vma is actually beyond the end of the search range,
    // there is no intersection.
    if (start->start() >= r.end()) {
        return {vmas.end(), vmas.end()};
    }
    // end is the first vma starting >= r.end(), so any previous vma (after
    // start) surely started < r.end() so is part of the intersection.
    auto end = vmas.lower_bound(r.end(), addr_compare());
    return {start, end};
}


/**
 * Change virtual memory range protection
 *
 * Change protection for a virtual memory range.  Updates page tables and VMas
 * for populated memory regions and just VMAs for unpopulated ranges.
 *
 * \return returns EACCESS/EPERM if requested permission cannot be granted
 */
static error protect(const void *addr, size_t size, unsigned int perm)
{
    uintptr_t start = reinterpret_cast<uintptr_t>(addr);
    uintptr_t end = start + size;
    auto range = find_intersecting_vmas(addr_range(start, end));
    for (auto i = range.first; i != range.second; ++i) {
        if (i->perm() == perm)
            continue;
        int err = i->validate_perm(perm);
        if (err != 0) {
            return make_error(err);
        }
        i->split(end);
        i->split(start);
        if (contains(start, end, *i)) {
            i->protect(perm);
            i->operate_range(protection(perm));
        }
    }
    return no_error();
}

class vma_range_addr_compare {
public:
    bool operator()(const vma_range& x, uintptr_t y) const { return x.start() < y; }
    bool operator()(uintptr_t x, const vma_range& y) const { return x < y.start(); }
};

uintptr_t find_hole(uintptr_t start, uintptr_t size)
{
    bool small = size < huge_page_size;
    uintptr_t good_enough = 0;

    vma_range_set_type &ranges = cur_vma_range_set();
    SCOPE_LOCK(cur_vma_range_set_mutex().for_read());
    //Find first vma range which starts before the start parameter or is the 1st one
    auto p = std::lower_bound(ranges.begin(), ranges.end(), start, vma_range_addr_compare());
    if (p != ranges.begin()) {
        --p;
    }
    auto n = std::next(p);
    while (n->start() <= upper_vma_limit) { //we only go up to the upper mmap vma limit
        //See if desired hole fits between p and n vmas
        if (start >= p->end() && start + size <= n->start()) {
            return start;
        }
        //See if shifting start to the end of p makes desired hole fit between p and n
        if (p->end() >= start && n->start() - p->end() >= size) {
            good_enough = p->end();
            if (small) {
                return good_enough;
            }
            //See if huge hole fits between p and n
            if (n->start() - align_up(good_enough, huge_page_size) >= size) {
                return align_up(good_enough, huge_page_size);
            }
        }
        //If nothing worked move next in the list
        p = n;
        ++n;
    }
    if (good_enough) {
        return good_enough;
    }
    throw make_error(ENOMEM);
}

ulong evacuate(uintptr_t start, uintptr_t end)
{
    auto range = find_intersecting_vmas(addr_range(start, end));
    ulong ret = 0;
    for (auto i = range.first; i != range.second; ++i) {
        i->split(end);
        i->split(start);
        if (contains(start, end, *i)) {
            auto& dead = *i--;
            auto size = dead.operate_range(unpopulate<account_opt::yes>(dead.page_ops()));
            ret += size;
#if CONF_memory_jvm_balloon
            if (dead.has_flags(mmap_jvm_heap)) {
                memory::stats::on_jvm_heap_free(size);
            }
#endif
            vma_list_type &vmas = cur_vma_list();
            vmas.erase(dead);
            WITH_LOCK(cur_vma_range_set_mutex().for_write()) {
                cur_vma_range_set().erase(vma_range(&dead));
            }
            delete &dead;
        }
    }
    return ret;
    // FIXME: range also indicates where we can insert a new anon_vma, use it
}

static void unmap(const void* addr, size_t size)
{
    size = align_up(size, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    evacuate(start, start+size);
}

static error sync(const void* addr, size_t length, int flags)
{
    length = align_up(length, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto end = start+length;
    auto err = make_error(ENOMEM);
    auto range = find_intersecting_vmas(addr_range(start, end));
    for (auto i = range.first; i != range.second; ++i) {
        err = i->sync(std::max(start, i->start()), std::min(end, i->end()));
        if (err.bad()) {
            break;
        }
    }
    return err;
}

class uninitialized_anonymous_page_provider : public page_allocator {
private:
    virtual void* fill(void* addr, uint64_t offset, uintptr_t size) {
        return addr;
    }
    template<int N>
    bool set_pte(void *addr, hw_ptep<N> ptep, pt_element<N> pte) {
        if (!addr) {
            throw std::exception();
        }
        if (!write_pte(addr, ptep, make_empty_pte<N>(), pte)) {
            if (pt_level_traits<N>::large_capable::value) {
                memory::free_huge_page(addr, pt_level_traits<N>::size::value);
            } else {
                memory::free_page(addr);
            }
            return false;
        }
        return true;
    }
public:
    virtual bool map(uintptr_t offset, hw_ptep<0> ptep, pt_element<0> pte, bool write) override {
        return set_pte(fill(memory::alloc_page(), offset, page_size), ptep, pte);
    }
    virtual bool map(uintptr_t offset, hw_ptep<1> ptep, pt_element<1> pte, bool write) override {
        size_t size = pt_level_traits<1>::size::value;
        return set_pte(fill(memory::alloc_huge_page(size), offset, size), ptep, pte);
    }
    virtual bool unmap(void *addr, uintptr_t offset, hw_ptep<0> ptep) override {
        clear_pte(ptep);
        return true;
    }
    virtual bool unmap(void *addr, uintptr_t offset, hw_ptep<1> ptep) override {
        clear_pte(ptep);
        return true;
    }
};

class initialized_anonymous_page_provider : public uninitialized_anonymous_page_provider {
private:
    virtual void* fill(void* addr, uint64_t offset, uintptr_t size) override {
        if (addr) {
            memset(addr, 0, size);
        }
        return addr;
    }
};

// Page provider for MAP_HUGETLB strict mode: refuses 4KB small-page fallback.
// When alloc_huge_page() fails, the level-1 map() throws (existing behaviour).
// The page walker then falls to level 0; our override returns false so
// populate_vma() does not account those bytes, letting map_anon() detect
// mapped < size and return ENOMEM to the caller.
class huge_only_page_provider : public initialized_anonymous_page_provider {
public:
    virtual bool map(uintptr_t offset, hw_ptep<0> ptep,
                     pt_element<0> pte, bool write) override {
        return false;   // reject small-page (4KB) fallback
    }
};
static huge_only_page_provider page_allocator_huge_only;
static page_allocator *page_allocator_huge_onlyp = &page_allocator_huge_only;

class map_file_page_read : public uninitialized_anonymous_page_provider {
private:
    file *_file;
    f_offset foffset;

    virtual void* fill(void* addr, uint64_t offset, uintptr_t size) override {
        if (addr) {
            iovec iovec {addr, size};
            uio data {&iovec, 1, off_t(foffset + offset), ssize_t(size), UIO_READ};
            _file->read(&data, FOF_OFFSET);
            /* zero buffer tail on a short read */
            if (data.uio_resid) {
                size_t tail = std::min(size, size_t(data.uio_resid));
                memset((char*)addr + size - tail, 0, tail);
            }
        }
        return addr;
    }
public:
    map_file_page_read(file *file, f_offset foffset) :
        _file(file), foffset(foffset) {}
    virtual ~map_file_page_read() {};
};

class map_file_page_mmap : public page_allocator {
private:
    file* _file;
    off_t _foffset;
    bool _shared;

public:
    map_file_page_mmap(file *file, off_t off, bool shared) : _file(file), _foffset(off), _shared(shared) {}
    virtual ~map_file_page_mmap() {};

    virtual bool map(uintptr_t offset, hw_ptep<0> ptep,  pt_element<0> pte, bool write) override {
        return _file->map_page(offset + _foffset, ptep, pte, write, _shared);
    }
    virtual bool map(uintptr_t offset, hw_ptep<1> ptep, pt_element<1> pte, bool write) override {
        return _file->map_page(offset + _foffset, ptep, pte, write, _shared);
    }
    virtual bool unmap(void *addr, uintptr_t offset, hw_ptep<0> ptep) override {
        return _file->put_page(addr, offset + _foffset, ptep);
    }
    virtual bool unmap(void *addr, uintptr_t offset, hw_ptep<1> ptep) override {
        return _file->put_page(addr, offset + _foffset, ptep);
    }
};

uintptr_t allocate(vma *v, uintptr_t start, size_t size, bool search)
{
    if (search) {
        // search for unallocated hole around start
        if (!start) {
            start = 0x200000000000ul;
        }
        start = find_hole(start, size);
    } else {
        // we don't know if the given range is free, need to evacuate it first
        evacuate(start, start+size);
    }
    v->set(start, start+size);

    cur_vma_list().insert(*v);
    WITH_LOCK(cur_vma_range_set_mutex().for_write()) {
        cur_vma_range_set().insert(vma_range(v));
    }

    return start;
}

inline bool in_vma_range(void* addr)
{
    return addr < debug_base;
}

void vpopulate(void* addr, size_t size)
{
    assert(!in_vma_range(addr));
    WITH_LOCK(page_table_high_mutex) {
        initialized_anonymous_page_provider map;
        operate_range(populate<>(&map, perm_rwx), addr, size);
    }
}

void vdepopulate(void* addr, size_t size)
{
    assert(!in_vma_range(addr));
    WITH_LOCK(page_table_high_mutex) {
        initialized_anonymous_page_provider map;
        operate_range(unpopulate<>(&map), addr, size);
    }
}

void vcleanup(void* addr, size_t size)
{
    assert(!in_vma_range(addr));
    WITH_LOCK(page_table_high_mutex) {
        cleanup_intermediate_pages cleaner;
        operate_range(cleaner, addr, addr, size);
    }
}

static void depopulate(void* addr, size_t length)
{
    length = align_up(length, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto range = find_intersecting_vmas(addr_range(start, start + length));
    for (auto i = range.first; i != range.second; ++i) {
        i->operate_range(unpopulate<>(i->page_ops()), reinterpret_cast<void*>(start), std::min(length, i->size()));
        start += i->size();
        length -= i->size();
    }
}

static void nohugepage(void* addr, size_t length)
{
    length = align_up(length, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto range = find_intersecting_vmas(addr_range(start, start + length));
    for (auto i = range.first; i != range.second; ++i) {
        if (!i->has_flags(mmap_small)) {
            i->update_flags(mmap_small);
            i->operate_range(splithugepages(), reinterpret_cast<void*>(start), std::min(length, i->size()));
        }
        start += i->size();
        length -= i->size();
    }
}

// Re-enable huge pages for a VMA range (inverse of nohugepage).
// Clears the mmap_small flag so subsequent page faults use 2MB huge pages.
// Already-faulted 4KB pages remain mapped until evicted; new faults use huge pages.
static void hugepage(void* addr, size_t length)
{
    length = align_up(length, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto range = find_intersecting_vmas(addr_range(start, start + length));
    for (auto i = range.first; i != range.second; ++i) {
        if (i->has_flags(mmap_small)) {
            i->clear_flags(mmap_small);
        }
        start += i->size();
        length -= i->size();
    }
}

error advise(void* addr, size_t size, int advice)
{
    PREVENT_STACK_PAGE_FAULT
    WITH_LOCK(cur_vma_list_mutex().for_write()) {
        if (!ismapped(addr, size)) {
            return make_error(ENOMEM);
        }
        if (advice == advise_dontneed) {
            depopulate(addr, size);
            return no_error();
        } else if (advice == advise_nohugepage) {
            nohugepage(addr, size);
            return no_error();
        } else if (advice == advise_hugepage) {
            hugepage(addr, size);
            return no_error();
        }
        return make_error(EINVAL);
    }
}

template<account_opt Account = account_opt::no>
ulong populate_vma(vma *vma, void *v, size_t size, bool write = false)
{
    page_allocator *map = vma->page_ops();
    auto total = vma->has_flags(mmap_small) ?
        vma->operate_range(populate_small<Account>(map, vma->perm(), write, vma->map_dirty()), v, size) :
        vma->operate_range(populate<Account>(map, vma->perm(), write, vma->map_dirty()), v, size);

    // On some architectures, the cpu data and instruction caches are separate (non-unified)
    // and therefore it might be necessary to synchronize data cache with instruction cache
    // after populating vma with executable code.
    if (vma->perm() & perm_exec) {
        synchronize_cpu_caches(v, size);
    }

    return total;
}

void* map_anon(const void* addr, size_t size, unsigned flags, unsigned perm)
{
    bool search = !(flags & mmap_fixed);
    size = align_up(size, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto* vma = new mmu::anon_vma(addr_range(start, start + size), perm, flags);
    PREVENT_STACK_PAGE_FAULT
    SCOPE_LOCK(cur_vma_list_mutex().for_write());
    auto v = (void*) allocate(vma, start, size, search);
    if (flags & mmap_populate) {
        auto mapped = populate_vma<account_opt::yes>(vma, v, size);
        if ((flags & mmap_huge) && mapped < size) {
            // MAP_HUGETLB strict mode: huge page allocation failed for some pages.
            // Free the partially-mapped region and signal ENOMEM to the caller.
            // Evacuate the address actually chosen by allocate(), not the
            // requested hint (which is 0 for an unhinted mmap).
            auto allocated = reinterpret_cast<uintptr_t>(v);
            evacuate(allocated, allocated + size);
            return nullptr;
        }
    }
    return v;
}

std::unique_ptr<file_vma> default_file_mmap(file* file, addr_range range, unsigned flags, unsigned perm, off_t offset)
{
    return std::unique_ptr<file_vma>(new file_vma(range, perm, flags, file, offset, new map_file_page_read(file, offset)));
}

std::unique_ptr<file_vma> map_file_mmap(file* file, addr_range range, unsigned flags, unsigned perm, off_t offset)
{
    return std::unique_ptr<file_vma>(new file_vma(range, perm, flags, file, offset, new map_file_page_mmap(file, offset, flags & mmap_shared)));
}

void* map_file(const void* addr, size_t size, unsigned flags, unsigned perm,
              fileref f, f_offset offset)
{
    bool search = !(flags & mmu::mmap_fixed);
    size = align_up(size, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto *vma = f->mmap(addr_range(start, start + size), flags | mmap_file, perm, offset).release();
    void *v;
    PREVENT_STACK_PAGE_FAULT
    WITH_LOCK(cur_vma_list_mutex().for_write()) {
        v = (void*) allocate(vma, start, size, search);
        if (flags & mmap_populate) {
            populate_vma(vma, v, std::min(size, align_up(::size(f), page_size)));
        }
    }
    return v;
}

bool is_linear_mapped(const void *addr, size_t size)
{
    if ((addr >= elf_start) && (addr + size <= elf_start + elf_size)) {
        return true;
    }
    return addr >= phys_mem;
}

// Checks if the entire given memory region is mmap()ed (in vma_list).
bool ismapped(const void *addr, size_t size)
{
    uintptr_t start = (uintptr_t) addr;
    uintptr_t end = start + size;

    auto range = find_intersecting_vmas(addr_range(start, end));
    for (auto p = range.first; p != range.second; ++p) {
        if (p->start() > start)
            return false;
        start = p->end();
        if (start >= end)
            return true;
    }
    return false;
}

// Checks if the entire given memory region is readable.
bool isreadable(void *addr, size_t size)
{
    char *end = align_up((char *)addr + size, mmu::page_size);
    char tmp;
    for (char *p = (char *)addr; p < end; p += mmu::page_size) {
        if (!safe_load(p, tmp))
            return false;
    }
    return true;
}

bool access_fault(vma& vma, unsigned int error_code)
{
    auto perm = vma.perm();
    if (mmu::is_page_fault_insn(error_code)) {
        return !(perm & perm_exec);
    }

    if (mmu::is_page_fault_write(error_code)) {
        return !(perm & perm_write);
    }

    return !(perm & perm_read);
}

TRACEPOINT(trace_mmu_vm_fault, "addr=%p, error_code=%x", uintptr_t, unsigned int);
TRACEPOINT(trace_mmu_vm_fault_sigsegv, "addr=%p, error_code=%x, %s", uintptr_t, unsigned int, const char*);
TRACEPOINT(trace_mmu_vm_fault_ret, "addr=%p, error_code=%x", uintptr_t, unsigned int);
#if CONF_lazy_stack
TRACEPOINT(trace_mmu_vm_stack_fault, "thread=%d, addr=%p, page_no=%d", unsigned int, uintptr_t, unsigned int);
#endif

static void vm_sigsegv(uintptr_t addr, exception_frame* ef)
{
    void *pc = ef->get_pc();
    if (pc >= text_start && pc < text_end) {
        debug_ll("page fault outside application, addr: 0x%016lx\n", addr);
        dump_registers(ef);
        abort();
    }
    osv::handle_mmap_fault(addr, SIGSEGV, ef);
}

static void vm_sigbus(uintptr_t addr, exception_frame* ef)
{
    osv::handle_mmap_fault(addr, SIGBUS, ef);
}

// --- fork COW write-fault resolver ----------------------------------------
#if CONF_fork
//
// Walk the CURRENT address space's page table to the leaf PTE for `addr`.  If
// it is a copy-on-write page (write-protected + cow bit, set by
// clone_address_space), allocate a fresh page, copy the shared page's contents
// into it, and install it writable in this AS only -- so the faulting side
// (parent or child) gets its own private copy.  Returns true if it handled a
// COW fault.
static bool handle_cow_write_fault(uintptr_t addr)
{
    // current_pt_root() returns the synthetic top (level-4) entry; follow it to
    // the PML4 page (level-3 entries) and walk down to the 4K leaf (level 0).
    pt_element<4> *top = current_pt_root();
    if (top->empty()) return false;
    auto pml4 = phys_cast<pt_element<3>>(top->next_pt_addr());
    unsigned i3 = pt_index(reinterpret_cast<void*>(addr), 3); // PML4 index
    pt_element<3> e3 = pml4[i3];
    if (e3.empty() || e3.large()) return false;
    auto pdpt = phys_cast<pt_element<2>>(e3.next_pt_addr());
    unsigned i2 = pt_index(reinterpret_cast<void*>(addr), 2);
    pt_element<2> e2 = pdpt[i2];
    if (e2.empty() || e2.large()) return false;
    auto pd = phys_cast<pt_element<1>>(e2.next_pt_addr());
    unsigned i1 = pt_index(reinterpret_cast<void*>(addr), 1);
    pt_element<1> e1 = pd[i1];
    if (e1.empty() || e1.large()) return false;   // 2 MB pages are not COW here
    auto pt = phys_cast<pt_element<0>>(e1.next_pt_addr());
    unsigned i0 = pt_index(reinterpret_cast<void*>(addr), 0);
    pt_element<0> e0 = pt[i0];
    if (e0.empty()) return false;
    if (!pte_is_cow(e0)) return false;

    // Copy the shared page into a fresh private page.
    void *shared = phys_to_virt(e0.addr());
    void *priv = memory::alloc_page();
    memcpy(priv, shared, page_size);

    // Install the private page writable, clearing the cow bit, in THIS AS only.
    pt_element<0> npte = make_leaf_pte(hw_ptep<0>::force(&pt[i0]),
                                       virt_to_phys(priv), perm_rwx);
    npte = pte_mark_cow(npte, false);
    npte.set_writable(true);
    pt[i0] = npte;
    mmu::flush_tlb_all();
    return true;
}
#endif // CONF_fork

void vm_fault(uintptr_t addr, exception_frame* ef)
{
#if CONF_fork
    // Page-fault servicing is KERNEL work: the pagecache pages, cached_page
    // bookkeeping and filesystem (e.g. ROFS) demand-paging buffers it allocates
    // are shared kernel infrastructure that must be identity-mapped and must
    // never be a COW fork-arena page.  Critically, this handler runs while
    // holding vma_list_mutex and may already be nested one exception deep;
    // touching a fresh (not-yet-faulted) arena page here would take ANOTHER
    // page fault, exceeding exception_depth and asserting.  Force the identity
    // kernel heap for the whole fault path so no arena page is ever allocated
    // or first-touched while servicing a fault.
    fork_arena::kernel_heap_scope kh;
#endif
    trace_mmu_vm_fault(addr, ef->get_error());
    if (fast_sigsegv_check(addr, ef)) {
        vm_sigsegv(addr, ef);
        trace_mmu_vm_fault_sigsegv(addr, ef->get_error(), "fast");
        return;
    }
#if CONF_lazy_stack
    auto stack = sched::thread::current()->get_stack_info();
    void *v_addr = reinterpret_cast<void*>(addr);
    if (v_addr >= stack.begin && v_addr < stack.begin + stack.size) {
        trace_mmu_vm_stack_fault(sched::thread::current()->id(), addr,
            ((u64)(stack.begin + stack.size - addr)) / 4096);
    }
#endif
    addr = align_down(addr, mmu::page_size);

#if CONF_fork
    // Resolve against the CURRENT thread's address space (child AS after fork,
    // else AS0).  A COW write fault copies the page privately for this side.
    address_space *as = kernel_address_space();
    {
        auto t = sched::thread::current();
        if (t && t->address_space()) {
            as = t->address_space();
        }
    }

    // First handle a copy-on-write write fault (fork private mappings): the
    // page is present but write-protected with the cow bit -- copy it.
    if (mmu::is_page_fault_write(ef->get_error())) {
        PREVENT_STACK_PAGE_FAULT
        WITH_LOCK(as->vmas_mutex->for_write()) {
            if (handle_cow_write_fault(addr)) {
                trace_mmu_vm_fault_ret(addr, ef->get_error());
                return;
            }
        }
    }

    WITH_LOCK(as->vmas_mutex->for_read()) {
        auto vma = find_intersecting_vma_in(*as->vmas, addr);
        if (vma == as->vmas->end() || access_fault(*vma, ef->get_error())) {
            vm_sigsegv(addr, ef);
            trace_mmu_vm_fault_sigsegv(addr, ef->get_error(), "slow");
            return;
        }
        vma->fault(addr, ef);
    }
    trace_mmu_vm_fault_ret(addr, ef->get_error());
#else // !CONF_fork -- original single-address-space fault path
    WITH_LOCK(vma_list_mutex.for_read()) {
        auto vma = find_intersecting_vma(addr);
        if (vma == vma_list.end() || access_fault(*vma, ef->get_error())) {
            vm_sigsegv(addr, ef);
            trace_mmu_vm_fault_sigsegv(addr, ef->get_error(), "slow");
            return;
        }
        vma->fault(addr, ef);
    }
    trace_mmu_vm_fault_ret(addr, ef->get_error());
#endif // CONF_fork
}

vma::vma(addr_range range, unsigned perm, unsigned flags, bool map_dirty, page_allocator *page_ops)
    : _range(align_down(range.start(), mmu::page_size), align_up(range.end(), mmu::page_size))
    , _perm(perm)
    , _flags(flags)
    , _map_dirty(map_dirty)
    , _page_ops(page_ops)
{
}

vma::~vma()
{
}

void vma::set(uintptr_t start, uintptr_t end)
{
    _range = addr_range(align_down(start, mmu::page_size), align_up(end, mmu::page_size));
}

void vma::protect(unsigned perm)
{
    _perm = perm;
}

uintptr_t vma::start() const
{
    return _range.start();
}

uintptr_t vma::end() const
{
    return _range.end();
}

void* vma::addr() const
{
    return reinterpret_cast<void*>(_range.start());
}

uintptr_t vma::size() const
{
    return _range.end() - _range.start();
}

unsigned vma::perm() const
{
    return _perm;
}

unsigned vma::flags() const
{
    return _flags;
}

void vma::update_flags(unsigned flag)
{
    assert(cur_vma_list_mutex().wowned());
    _flags |= flag;
}

void vma::clear_flags(unsigned flag)
{
    assert(cur_vma_list_mutex().wowned());
    _flags &= ~flag;
}

bool vma::has_flags(unsigned flag)
{
    return _flags & flag;
}

template<typename T> ulong vma::operate_range(T mapper, void *addr, size_t size)
{
    return mmu::operate_range(mapper, reinterpret_cast<void*>(start()), addr, size);
}

template<typename T> ulong vma::operate_range(T mapper)
{
    void *addr = reinterpret_cast<void*>(start());
    return mmu::operate_range(mapper, addr, addr, size());
}

bool vma::map_dirty()
{
    return _map_dirty;
}

void vma::fault(uintptr_t addr, exception_frame *ef)
{
    auto hp_start = align_up(_range.start(), huge_page_size);
    auto hp_end = align_down(_range.end(), huge_page_size);
    size_t size;
    if (!has_flags(
#if CONF_memory_jvm_balloon
mmap_jvm_balloon|
#endif
mmap_small) && (hp_start <= addr && addr < hp_end)) {
        addr = align_down(addr, huge_page_size);
        size = huge_page_size;
    } else {
        size = page_size;
    }

#if CONF_memory_jvm_balloon
    auto total =
#endif
    populate_vma<account_opt::yes>(this, (void*)addr, size,
        mmu::is_page_fault_write(ef->get_error()));

#if CONF_memory_jvm_balloon
    if (_flags & mmap_jvm_heap) {
        memory::stats::on_jvm_heap_alloc(total);
    }
#endif
}

page_allocator* vma::page_ops()
{
    return _page_ops;
}

static uninitialized_anonymous_page_provider page_allocator_noinit;
static initialized_anonymous_page_provider page_allocator_init;
static page_allocator *page_allocator_noinitp = &page_allocator_noinit, *page_allocator_initp = &page_allocator_init;

anon_vma::anon_vma(addr_range range, unsigned perm, unsigned flags)
    : vma(range, perm, flags, true,
          (flags & mmap_huge)          ? page_allocator_huge_onlyp :
          (flags & mmap_uninitialized) ? page_allocator_noinitp    :
                                         page_allocator_initp)
{
}

void anon_vma::split(uintptr_t edge)
{
    if (edge <= _range.start() || edge >= _range.end()) {
        return;
    }
    vma* n = new anon_vma(addr_range(edge, _range.end()), _perm, _flags);
    set(_range.start(), edge);
    cur_vma_list().insert(*n);
    WITH_LOCK(cur_vma_range_set_mutex().for_write()) {
        cur_vma_range_set().insert(vma_range(n));
    }
}

error anon_vma::sync(uintptr_t start, uintptr_t end)
{
    return no_error();
}

#if CONF_memory_jvm_balloon
// Balloon is backed by no pages, but in the case of partial copy, we may have
// to back some of the pages. For that and for that only, we initialize a page
// allocator. It is fine in this case to use the noinit allocator. Since this
// area was supposed to be holding the balloon object before, so the JVM will
// not count on it being initialized to any value.
jvm_balloon_vma::jvm_balloon_vma(unsigned char *jvm_addr, uintptr_t start,
                                 uintptr_t end, balloon_ptr b, unsigned perm, unsigned flags)
    : vma(addr_range(start, end), perm_rw, flags | mmap_jvm_balloon, true, page_allocator_noinitp),
      _balloon(b), _jvm_addr(jvm_addr),
      _real_perm(perm), _real_flags(flags & ~mmap_jvm_balloon), _real_size(end - start)
{
}

// IMPORTANT: This code assumes that opportunistic copying never happens during
// partial copying.  In general, this assumption is wrong. There is nothing
// that prevents the JVM from doing both at the same time from the same object.
// However, hotspot seems not to do it, which simplifies a lot our code.
//
// If that assumption fails to hold in some real life scenario (be it a hotspot
// corner case or another JVM), the assertion eff == _effective_jvm_addr will
// crash us and we will find it out.  If we need it, it is not impossible to
// handle this case: all we have to do is create a list of effective addresses
// and keep the partial counts independently.
//
// Explanation about partial copy:
//
// There are situations during which some Garbage Collectors will copy a large
// object in parallel, using various threads, each being responsible for a part
// of the object.
//
// If that happens, the simple balloon move algorithm will break. However,
// because offset 'x' in the source will always be copied to offset 'x' in the
// destination, we can still calculate the final destination object. This
// address is the _effective_jvm_addr in the code below.
//
// The problem is that we cannot open the new balloon yet. Since the JVM
// believes it is copying only a part of the object, the destination may (and
// usually will) contain valid objects, that need to be themselves moved
// somewhere else before we can install our object there.
//
// Also, we can't close the object fully when someone writes to it: because a
// part of the object is now already freed, the JVM may and will go ahead and
// copy another object to this location. To handle this case, we use the
// variable _partial_copy, which keeps track of how much data has being copied
// from this location to somewhere else. Because we know that the JVM has to
// copy the whole object, when that counter reaches the amount of bytes we
// expect in this vma, this means we can close this object (assuming no
// opportunistic copy)
//
// It is also possible that the region will be written to during partial copy.
// Although it is invalid to overwrite pieces of the object, it is perfectly
// valid to write to locations that were already copied from. This is handled
// in the fault handler itself, by mapping pages to the location that currently
// holds the balloon vma. At some point, we will create an anonymous vma in its
// place.
bool jvm_balloon_vma::add_partial(size_t partial, unsigned char *eff)
{
    if (_effective_jvm_addr) {
        assert(eff == _effective_jvm_addr);
    } else {
        _effective_jvm_addr= eff;
    }

    _partial_copy += partial;
    return _partial_copy == real_size();
}

void jvm_balloon_vma::split(uintptr_t edge)
{
    abort();
}

error jvm_balloon_vma::sync(uintptr_t start, uintptr_t end)
{
    return no_error();
}

void jvm_balloon_vma::fault(uintptr_t fault_addr, exception_frame *ef)
{
    if (memory::balloon_api && memory::balloon_api->fault(_balloon, ef, this)) {
        return;
    }
    // Can only reach this case if we are doing partial copies
    assert(_effective_jvm_addr);
    // FIXME : This will always use a small page, due to the flag check we have
    // in vma::fault. We can try to map the original worker with a huge page,
    // and try to see if we succeed. Using a huge page is harder than it seems,
    // because the JVM is not guaranteed to copy objects in huge page
    // increments - and it usually won't.  If we go ahead and map a huge page
    // subsequent copies *from* this location will not fault and we will lose
    // track of the partial copy count.
    vma::fault(fault_addr, ef);
}

jvm_balloon_vma::~jvm_balloon_vma()
{
    // it believes the objects are no longer valid. It could be the case
    // for a dangling mapping representing a balloon that was already moved
    // out.
    vma_list.erase(*this);
    WITH_LOCK(vma_range_set_mutex.for_write()) {
        vma_range_set.erase(vma_range(this));
    }
    assert(!(_real_flags & mmap_jvm_balloon));
    mmu::map_anon(addr(), size(), _real_flags, _real_perm);

    if (_effective_jvm_addr) {
        // Can't just use size(), because although rare, the source and destination can
        // have different alignments
        auto end = align_down(_effective_jvm_addr + memory::balloon_size, memory::balloon_alignment);
        auto s = end - align_up(_effective_jvm_addr, memory::balloon_alignment);
        mmu::map_jvm(_effective_jvm_addr, s, mmu::huge_page_size, _balloon);
    }
}

ulong map_jvm(unsigned char* jvm_addr, size_t size, size_t align, balloon_ptr b)
{
    auto addr = align_up(jvm_addr, align);
    auto start = reinterpret_cast<uintptr_t>(addr);

    vma* v;
    WITH_LOCK(vma_list_mutex.for_read()) {
        u64 a = reinterpret_cast<u64>(addr);
        v = &*find_intersecting_vma(a);

        // It has to be somewhere!
        assert(v != &*vma_list.end());
        assert(v->has_flags(mmap_jvm_heap) | v->has_flags(mmap_jvm_balloon));
        if (v->has_flags(mmap_jvm_balloon) && (v->addr() == addr)) {
            jvm_balloon_vma *j = static_cast<jvm_balloon_vma *>(&*v);
            if (&*j->_balloon != &*b) {
                j->_balloon = b;
            }
            return 0;
        }
    }

    auto* vma = new mmu::jvm_balloon_vma(jvm_addr, start, start + size, b, v->perm(), v->flags());

    PREVENT_STACK_PAGE_FAULT
    WITH_LOCK(vma_list_mutex.for_write()) {
        // This means that the mapping that we had before was a balloon mapping
        // that was laying around and wasn't updated to an anon mapping. If we
        // allow it to split it would significantly complicate our code, since
        // now the finishing code would have to deal with the case where the
        // bounds found in the vma are not the real bounds. We delete it right
        // away and avoid it altogether.
        auto range = find_intersecting_vmas(addr_range(start, start + size));

        for (auto i = range.first; i != range.second; ++i) {
            if (i->has_flags(mmap_jvm_balloon)) {
                jvm_balloon_vma *jvma = static_cast<jvm_balloon_vma *>(&*i);
                // If there is an effective address this means this is a
                // partial copy. We cannot close it here because the copy is
                // still ongoing. We can, though, assume that if we are
                // installing a new vma over a part of this region, that
                // particular part was already copied to in the original
                // balloon.
                //
                // FIXME: This is solvable by reducing the size of the vma and
                // keeping track of the original size. Still, we can't really
                // call the split code directly because that will delete the
                // vma and cause its termination
                if (jvma->effective_jvm_addr() != nullptr) {
                    auto end = start + size;
                    // Should have exited before the creation of the vma,
                    // just updating the balloon pointer.
                    assert(jvma->start() != start);
                    // Since we will change its position in the tree, for the sake of future
                    // lookups we need to reinsert it.
                    vma_list.erase(*jvma);
                    WITH_LOCK(vma_range_set_mutex.for_write()) {
                        vma_range_set.erase(vma_range(jvma));
                    }
                    if (jvma->start() < start) {
                        assert(jvma->partial() >= (jvma->end() - start));
                        jvma->set(jvma->start(), start);
                    } else {
                        assert(jvma->partial() >= (end - jvma->start()));
                        jvma->set(end, jvma->end());
                    }
                    vma_list.insert(*jvma);
                    WITH_LOCK(vma_range_set_mutex.for_write()) {
                        vma_range_set.insert(vma_range(jvma));
                    }
                } else {
                    // Note how v and jvma are different. This is because this one,
                    // we will delete.
                    auto& v = *i--;
                    vma_list.erase(v);
                    WITH_LOCK(vma_range_set_mutex.for_write()) {
                        vma_range_set.erase(vma_range(&v));
                    }
                    // Finish the move. In practice, it will temporarily remap an
                    // anon mapping here, but this should be rare. Let's not
                    // complicate the code to optimize it. There are no
                    // guarantees that we are talking about the same balloon If
                    // this is the old balloon
                    delete &v;
                }
            }
        }

        evacuate(start, start + size);
        vma_list.insert(*vma);
        WITH_LOCK(vma_range_set_mutex.for_write()) {
            vma_range_set.insert(vma_range(vma));
        }
        return vma->size();
    }
    return 0;
}
#endif

file_vma::file_vma(addr_range range, unsigned perm, unsigned flags, fileref file, f_offset offset, page_allocator* page_ops)
    : vma(range, perm, flags | mmap_small, !(flags & mmap_shared), page_ops)
    , _file(file)
    , _offset(offset)
{
    int err = validate_perm(perm);

    if (err != 0) {
        throw make_error(err);
    }

    struct stat st;
    err = _file->stat(&st);
    if (err != 0) {
        throw make_error(err);
    }

    _file_inode = st.st_ino;
    _file_dev_id = st.st_dev;
}

void file_vma::fault(uintptr_t addr, exception_frame *ef)
{
    auto hp_start = align_up(_range.start(), huge_page_size);
    auto hp_end = align_down(_range.end(), huge_page_size);
    auto fsize = ::size(_file);
    if (offset(addr) >= fsize) {
        vm_sigbus(addr, ef);
        return;
    }
    size_t size;
    if (!has_flags(mmap_small) && (hp_start <= addr && addr < hp_end) && offset(hp_end) < fsize) {
        addr = align_down(addr, huge_page_size);
        size = huge_page_size;
    } else {
        size = page_size;
    }

    populate_vma<account_opt::no>(this, (void*)addr, size,
            mmu::is_page_fault_write(ef->get_error()));
}

file_vma::~file_vma()
{
    delete _page_ops;
}

void file_vma::split(uintptr_t edge)
{
    if (edge <= _range.start() || edge >= _range.end()) {
        return;
    }
    auto off = offset(edge);
    vma *n = _file->mmap(addr_range(edge, _range.end()), _flags, _perm, off).release();
    set(_range.start(), edge);
    cur_vma_list().insert(*n);
    WITH_LOCK(cur_vma_range_set_mutex().for_write()) {
        cur_vma_range_set().insert(vma_range(n));
    }
}

error file_vma::sync(uintptr_t start, uintptr_t end)
{
    if (!has_flags(mmap_shared))
        return make_error(ENOMEM);

    // Called when ZFS arc cache is not present.
    if (_page_ops && dynamic_cast<map_file_page_read *>(_page_ops)) {
        start = std::max(start, _range.start());
        end = std::min(end, _range.end());
        uintptr_t size = end - start;

        dirty_page_sync sync(_file.get(), _offset, ::size(_file));
        error err = no_error();
        try {
            if (operate_range(dirty_cleaner<dirty_page_sync, account_opt::yes>(sync), (void*)start, size) != 0) {
                err = make_error(sys_fsync(_file.get()));
            }
        } catch (error e) {
            err = e;
        }
        return err;
    }

    try {
        _file->sync(_offset + start - _range.start(), _offset + end - _range.start());
    } catch (error& err) {
        return err;
    }

    return make_error(sys_fsync(_file.get()));
}

int file_vma::validate_perm(unsigned perm)
{
    // fail if mapping a file that is not opened for reading.
    if (!(_file->f_flags & FREAD)) {
        return EACCES;
    }
    if (perm & perm_write) {
        if (has_flags(mmap_shared) && !(_file->f_flags & FWRITE)) {
            return EACCES;
        }
    }
    // fail if prot asks for PROT_EXEC and the underlying FS was
    // mounted no-exec.
    if (perm & perm_exec && (_file->f_dentry->d_mount->m_flags & MNT_NOEXEC)) {
        return EPERM;
    }
    return 0;
}

f_offset file_vma::offset(uintptr_t addr)
{
    return _offset + (addr - _range.start());
}

std::unique_ptr<file_vma> shm_file::mmap(addr_range range, unsigned flags, unsigned perm, off_t offset)
{
    return map_file_mmap(this, range, flags, perm, offset);
}

void* shm_file::page(uintptr_t hp_off)
{
    void *addr;

    auto p = _pages.find(hp_off);
    if (p == _pages.end()) {
        addr = memory::alloc_huge_page(huge_page_size);
        if (!addr)
            throw make_error(ENOMEM);
        memset(addr, 0, huge_page_size);
        _pages.emplace(hp_off, addr);
    } else {
        addr = p->second;
    }

    return addr;
}

bool shm_file::map_page(uintptr_t offset, hw_ptep<0> ptep, pt_element<0> pte, bool write, bool shared)
{
    uintptr_t hp_off = align_down(offset, huge_page_size);

    return write_pte(static_cast<char*>(page(hp_off)) + offset - hp_off, ptep, pte);
}

bool shm_file::map_page(uintptr_t offset, hw_ptep<1> ptep, pt_element<1> pte, bool write, bool shared)
{
    uintptr_t hp_off = align_down(offset, huge_page_size);

    assert(hp_off == offset);

    return write_pte(static_cast<char*>(page(hp_off)) + offset - hp_off, ptep, pte);
}

bool shm_file::put_page(void *addr, uintptr_t offset, hw_ptep<0> ptep) {
    // Clear the PTE so the virtual mapping is removed. Return false so the
    // TLB gather does not free the physical page — shm_file::close() owns it.
    clear_pte(ptep);
    return false;
}
bool shm_file::put_page(void *addr, uintptr_t offset, hw_ptep<1> ptep) {
    clear_pte(ptep);
    return false;
}

shm_file::shm_file(size_t size, int flags) : special_file(flags, DTYPE_UNSPEC), _size(size) {}

int shm_file::stat(struct stat* buf)
{
    buf->st_size = _size;
    return 0;
}

int shm_file::truncate(off_t len)
{
    if (len < 0)
        return EINVAL;
    _size = (size_t)len;
    return 0;
}

int shm_file::close()
{
    for (auto& i : _pages) {
        memory::free_huge_page(i.second, huge_page_size);
    }
    _pages.clear();
    return 0;
}

linear_vma::linear_vma(void* virt, phys phys, size_t size, mattr mem_attr, const char* name) {
    _virt_addr = virt;
    _phys_addr = phys;
    _size = size;
    _mem_attr = mem_attr;
    _name = name;
}

linear_vma::~linear_vma() {
}

std::string sysfs_linear_maps() {
    std::string output;
    WITH_LOCK(linear_vma_set_mutex.for_read()) {
        for(auto *vma : linear_vma_set) {
            char mattr = vma->_mem_attr == mmu::mattr::normal ? 'n' : 'd';
            output += osv::sprintf("%18p %18p %12x rwxp %c %s\n",
                vma->_virt_addr, (void*)vma->_phys_addr, vma->_size, mattr, vma->_name.c_str());
        }
    }
    return output;
}

void linear_map(void* _virt, phys addr, size_t size, const char* name,
                size_t slop, mattr mem_attr)
{
    uintptr_t virt = reinterpret_cast<uintptr_t>(_virt);
    slop = std::min(slop, page_size_level(nr_page_sizes - 1));
    assert((virt & (slop - 1)) == (addr & (slop - 1)));
    linear_page_mapper phys_map(addr, size, mem_attr);
    map_range(virt, virt, size, phys_map, slop);
    auto _vma = new linear_vma(_virt, addr, size, mem_attr, name);
    WITH_LOCK(linear_vma_set_mutex.for_write()) {
       linear_vma_set.insert(_vma);
    }
    WITH_LOCK(vma_range_set_mutex.for_write()) {
       vma_range_set.insert(vma_range(_vma));
    }
}

void free_initial_memory_range(uintptr_t addr, size_t size)
{
    if (!size) {
        return;
    }
    // Most of the time the kernel code references memory using
    // virtual addresses. However some allocated system structures
    // like page tables use physical addresses.
    // For that reason we skip the very 1st page of physical memory
    // so that allocated memory areas NEVER map to physical address 0.
    if (!addr) {
        ++addr;
        --size;
    }
    memory::free_initial_memory_range(phys_cast<void>(addr), size);
}

error mprotect(const void *addr, size_t len, unsigned perm)
{
    PREVENT_STACK_PAGE_FAULT
    SCOPE_LOCK(cur_vma_list_mutex().for_write());

    if (!ismapped(addr, len)) {
        return make_error(ENOMEM);
    }

    return protect(addr, len, perm);
}

error munmap(const void *addr, size_t length)
{
    PREVENT_STACK_PAGE_FAULT
    SCOPE_LOCK(cur_vma_list_mutex().for_write());

    length = align_up(length, mmu::page_size);
    if (!ismapped(addr, length)) {
        return make_error(EINVAL);
    }
    sync(addr, length, 0);
    unmap(addr, length);
    return no_error();
}

error msync(const void* addr, size_t length, int flags)
{
    SCOPE_LOCK(cur_vma_list_mutex().for_read());

    if (!ismapped(addr, length)) {
        return make_error(ENOMEM);
    }
    return sync(addr, length, flags);
}

error mincore(const void *addr, size_t length, unsigned char *vec)
{
    char *end = align_up((char *)addr + length, page_size);
    char tmp;
    SCOPE_LOCK(cur_vma_list_mutex().for_read());
    if (!is_linear_mapped(addr, length) && !ismapped(addr, length)) {
        return make_error(ENOMEM);
    }
    for (char *p = (char *)addr; p < end; p += page_size) {
        if (safe_load(p, tmp)) {
            *vec++ = 0x01;
        } else {
            *vec++ = 0x00;
        }
    }
    return no_error();
}

std::string procfs_maps()
{
    std::string output;
    WITH_LOCK(vma_list_mutex.for_read()) {
        for (auto& vma : vma_list) {
            char read    = vma.perm() & perm_read  ? 'r' : '-';
            char write   = vma.perm() & perm_write ? 'w' : '-';
            char execute = vma.perm() & perm_exec  ? 'x' : '-';
            char priv    = 'p';
            output += osv::sprintf("%lx-%lx %c%c%c%c ", vma.start(), vma.end(), read, write, execute, priv);
            if (vma.flags() & mmap_file) {
                const file_vma &f_vma = static_cast<file_vma&>(vma);
                unsigned dev_id_major = major(f_vma.file_dev_id());
                unsigned dev_id_minor = minor(f_vma.file_dev_id());
                output += osv::sprintf("%08x %02x:%02x %ld %s\n", f_vma.offset(), dev_id_major, dev_id_minor, f_vma.file_inode(), f_vma.file()->f_dentry->d_path);
            } else {
                output += osv::sprintf("00000000 00:00 0\n");
            }
        }
    }
    return output;
}

}

extern "C" bool is_linear_mapped(const void *addr)
{
    return addr >= mmu::phys_mem;
}
