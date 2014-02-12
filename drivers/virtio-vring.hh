/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRTIO_VRING_H
#define VIRTIO_VRING_H

#include <atomic>
#include <functional>
#include <osv/mutex.h>
#include <osv/debug.hh>
#include <osv/mmu.hh>
#include <osv/sched.hh>

#define virtio_tag "virtio"
#define virtio_d(...)   tprintf_d(virtio_tag, __VA_ARGS__)
#define virtio_i(...)   tprintf_i(virtio_tag, __VA_ARGS__)
#define virtio_w(...)   tprintf_w(virtio_tag, __VA_ARGS__)
#define virtio_e(...)   tprintf_e(virtio_tag, __VA_ARGS__)

namespace virtio {

class virtio_vring;
class virtio_driver;

    // Buffer descriptors in the ring
    class vring_desc {
    public:
        enum {
            // Read only buffer
            VRING_DESC_F_READ=0,
            // This marks a buffer as continuing via the next field.
            VRING_DESC_F_NEXT=1,
            // This marks a buffer as write-only (otherwise read-only).
            VRING_DESC_F_WRITE=2,
            // This means the buffer contains a list of buffer descriptors.
            VRING_DESC_F_INDIRECT=4
        };

        u64 get_paddr();
        u32 get_len() { return _len; }
        u16 next_idx() { return _next; }

        // flags
        bool is_chained() { return (_flags & VRING_DESC_F_NEXT) == VRING_DESC_F_NEXT; };
        bool is_write() { return (_flags & VRING_DESC_F_WRITE) == VRING_DESC_F_WRITE; };
        bool is_indirect() { return (_flags & VRING_DESC_F_INDIRECT) == VRING_DESC_F_INDIRECT; };
        
        u64 _paddr;
        u32 _len;
        u16 _flags;
        u16 _next;
    };

    // Guest to host
    class vring_avail {
    public:
        enum {
            // Mark that we do not need an interrupt for consuming a descriptor
            // from the ring. Unrelieable so it's simply an optimization
            VRING_AVAIL_F_NO_INTERRUPT=1
        };

        void disable_interrupt() { _flags.store(VRING_AVAIL_F_NO_INTERRUPT, std::memory_order_relaxed); }
        void enable_interrupt() { _flags.store(0, std::memory_order_relaxed); }
        bool interrupt_on() { return (_flags.load(std::memory_order_relaxed) & VRING_AVAIL_F_NO_INTERRUPT) == 0;}

        std::atomic<u16> _flags;

        // Where we put the next descriptor
        std::atomic<u16> _idx;
        // There may be no more entries than the queue size read from device
        u16 _ring[];
        // used event index is an optimization in order to get an interrupt from the host
        // only when the value reaches this number
        // The location of this field is places after the variable length ring array,
        // that's why we cannot fully define it within the struct and use a function accessor
        //std::atomic<u16> used_event;
    };

    class vring_used_elem {
    public:
        // Index of start of used vring_desc chain. (u32 for padding reasons)
        u32 _id;
        // Total length of the descriptor chain which was used (written to)
        u32 _len;
    };

    // Host to guest
    class vring_used {
    public:

        enum {
            // The Host advise the Guest: don't kick me when
            // you add a buffer.  It's unreliable, so it's simply an 
            // optimization. Guest will still kick if it's out of buffers.
            VRING_USED_F_NO_NOTIFY=1
        };

        bool notifications_disabled() {
            return (_flags.load(std::memory_order_relaxed) & VRING_USED_F_NO_NOTIFY) != 0;
        }
        
        // Using std::atomic since it being changed by the host
        std::atomic<u16> _flags;
        // Using std::atomic in order to have memory barriers for it
        std::atomic<u16> _idx;
        vring_used_elem _used_elements[];
        // avail event index is an optimization kick the host only when the value reaches this number
        // The location of this field is places after the variable length ring array,
        // that's why we cannot fully define it within the struct and use a function accessor
        //std::atomic<u16> avail_event;
    };

    class vring {
    public:

        vring(virtio_driver* const dev, u16 num, u16 q_index);
        virtual ~vring();

        u64 get_paddr();
        static unsigned get_size(unsigned int num, unsigned long align);

        // Ring operations
        bool add_buf(void* cookie);
        // Get the top item from the used ring
        void* get_buf_elem(u32* len);
        // Let the host know we consumed the used entry
        // We separate that from get_buf_elem so no one
        // will re-cycle the request header location until
        // we're finished with it in the upper layer
        void get_buf_finalize();
        // GC the used items that were already read to be emptied
        // within the ring. Should be called by add_buf
        // It was separated from the get_buf flow to allow parallelism of the two
        void get_buf_gc();

        bool used_ring_not_empty() const;
        bool used_ring_is_half_empty() const;
        bool used_ring_can_gc() const;
        bool avail_ring_not_empty();
        // when the available ring has x descriptors as room it means that
        // x descriptors can be allocated while _num-x are available for the host
        bool avail_ring_has_room(int n);
        bool refill_ring_cond();
        bool use_indirect(int desc_needed);
        void set_use_indirect(bool flag) { _use_indirect = flag;}
        bool get_use_indirect() { return _use_indirect;}
        bool kick();
        // Total number of descriptors in ring
        int size() {return _num;}

        // Use memory order acquire when there are prior updates to local variables that must
        // be seen by the reading threads
        void set_used_event(u16 event, std::memory_order order) {_used_event->store(event, order);};


        // Let host know about interrupt delivery
        void disable_interrupts();
        void enable_interrupts();

        const int max_sgs = 256;
        struct sg_node {
            u64 _paddr;
            u32 _len;
            u16 _flags;
            sg_node(u64 addr, u32 len, u16 flags=0) :_paddr(addr), _len(len), _flags(flags) {};
            sg_node(const sg_node& n) :_paddr(n._paddr), _len(n._len), _flags(n._flags) {};
        };

        void init_sg()
        {
            _sg_vec.clear();
        }

        void add_out_sg(void* vaddr, u32 len)
        {
            u64 paddr = mmu::virt_to_phys(vaddr);
            _sg_vec.emplace_back(paddr, len, vring_desc::VRING_DESC_F_READ);
        }

        void add_in_sg(void* vaddr, u32 len)
        {
            u64 paddr = mmu::virt_to_phys(vaddr);
            _sg_vec.emplace_back(paddr, len, vring_desc::VRING_DESC_F_WRITE);
        }

        void add_buf_wait(void* cookie);

        void wakeup_waiter()
        {
            _waiter.wake();
        }


        // holds a temporary sg_nodes that travel between the upper layer virtio to add_buf
        std::vector<sg_node> _sg_vec;

        sched::thread_handle _waiter;

        u16 avail_head() const {return _avail_head;};

    private:

        // Up pointer
        virtio_driver* _dev;
        u16 _q_index;
        // The physical of the physical address handed to the virtio device
        void* _vring_ptr;
        
        // Total number of descriptors in ring
        unsigned int _num;

        // Position of the next available descriptor
        u16 _avail_head;
        // Position of the used descriptor we've last seen
        // from the host used ring
        u16 _used_ring_host_head;
        // Position of the used descriptor we've last seen
        // used internally for get-add bufs sync
        u16 _used_ring_guest_head;
        // The amount of avail descriptors we've added since last kick
        u16 _avail_added_since_kick;
        u16 _avail_count;

        // Flat list of chained descriptors
        vring_desc* _desc;
        // Available for host consumption
        vring_avail* _avail;
        // Available for guest consumption
        vring_used* _used;
        // cookies to store access to the upper layer pointers
        void** _cookie;
        //protects parallel get_bug /add_buf access, mainly the _avail_head variable
        mutex _lock;
        // pointer to the end of the used ring to get a glimpse of the host avail idx
        std::atomic<u16>* _avail_event;
        std::atomic<u16>* _used_event;
        // A flag set by driver to turn on/off indirect descriptor
        bool _use_indirect;
    };


}

#endif // VIRTIO_VRING_H
