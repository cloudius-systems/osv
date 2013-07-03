#include <string.h>
#include "mempool.hh"
#include "mmu.hh"

#include "virtio.hh"
#include "drivers/virtio-vring.hh"
#include "debug.hh"

#include "sched.hh"
#include "interrupt.hh"
#include "osv/trace.hh"

using namespace memory;
using sched::thread;

TRACEPOINT(trace_virtio_enable_interrupts, "vring=%p", void *);
TRACEPOINT(trace_virtio_disable_interrupts, "vring=%p", void *);
TRACEPOINT(trace_virtio_kick, "queue=%d", u16);

namespace virtio {

    vring::vring(virtio_driver* const dev, u16 num, u16 q_index)
    {
        _dev = dev;
        _q_index = q_index;
        // Alloc enough pages for the vring...
        unsigned sz = VIRTIO_ALIGN(vring::get_size(num, VIRTIO_PCI_VRING_ALIGN));
        _vring_ptr = malloc(sz);
        memset(_vring_ptr, 0, sz);
        
        // Set up pointers        
        _num = num;
        _desc = (vring_desc *)_vring_ptr;
        _avail = (vring_avail *)(_vring_ptr + num*sizeof(vring_desc));
        _used = (vring_used *)(((unsigned long)&_avail->_ring[num] + 
                sizeof(u16) + VIRTIO_PCI_VRING_ALIGN-1) & ~(VIRTIO_PCI_VRING_ALIGN-1));

        // initialize the next pointer within the available ring
        for (int i=0;i<num;i++) _desc[i]._next = i+1;
        _desc[num-1]._next = 0;

        _cookie = new void*[num];

        _avail_head = 0;
        _used_guest_head = 0;
        _avail_added_since_kick = 0;
        _avail_count = num;

        _avail_event = reinterpret_cast<std::atomic<u16>*>(&_used->_used_elements[_num]);
        _used_event = reinterpret_cast<std::atomic<u16>*>(&_avail->_ring[_num]);

        _sg_vec.reserve(max_sgs);
    }

    vring::~vring()
    {
        free(_vring_ptr);
        delete [] _cookie;
    }

    u64 vring::get_paddr(void)
    {
        return mmu::virt_to_phys(_vring_ptr);
    }

    unsigned vring::get_size(unsigned int num, unsigned long align)
    {
        return (((sizeof(vring_desc) * num + sizeof(u16) * (3 + num)
                 + align - 1) & ~(align - 1))
                + sizeof(u16) * 3 + sizeof(vring_used_elem) * num);
    }

    void vring::disable_interrupts()
    {
        trace_virtio_disable_interrupts(this);
        _avail->disable_interrupt();
    }

    bool vring::use_indirect(int desc_needed)
    {
        return false && // It degrades netperf performance
                _dev->get_indirect_buf_cap() &&
                desc_needed > 1 &&  					// no need to use indirect for a single descriptor
                _avail_count > _num*2/3;   // use indirect only when low space
    }

    void vring::enable_interrupts()
    {
        trace_virtio_enable_interrupts(this);
        _avail->enable_interrupt();
        set_used_event(_used_guest_head, std::memory_order_relaxed);
    }

    bool
    vring::add_buf(void* cookie) {
        return with_lock(_lock, [=] {
            int desc_needed = _sg_vec.size();
            bool indirect = false;
            if (use_indirect(desc_needed*_num/2)) {
                desc_needed = 1;
                indirect = true;
            }

            if (_avail_count < desc_needed) {
                //make sure the interrupts get there
                //it probably should force an exit to the host
                kick();
                return false;
            }

            int idx, prev_idx = -1;
            idx = _avail_head;

            _cookie[idx] = cookie;
            vring_desc* descp = _desc;

            if (indirect) {
                vring_desc* indirect = reinterpret_cast<vring_desc*>(malloc((_sg_vec.size())*sizeof(vring_desc)));
                if (!indirect)
                    return false;
                _desc[idx]._flags = vring_desc::VRING_DESC_F_INDIRECT;
                _desc[idx]._paddr = mmu::virt_to_phys(indirect);
                _desc[idx]._len = (_sg_vec.size())*sizeof(vring_desc);

                descp = indirect;
                //initialize the next pointers
                for (u32 j=0;j<_sg_vec.size();j++) descp[j]._next = j+1;
                //hack to make the logic below the for loop below act
                //just as before
                descp[_sg_vec.size()-1]._next = _desc[idx]._next;
                idx = 0;
            }

            for (unsigned i = 0; i < _sg_vec.size(); i++) {
                descp[idx]._flags = vring_desc::VRING_DESC_F_NEXT| _sg_vec[i]._flags;
                descp[idx]._paddr = _sg_vec[i]._paddr;
                descp[idx]._len = _sg_vec[i]._len;
                prev_idx = idx;
                idx = descp[idx]._next;
            }
            descp[prev_idx]._flags &= ~vring_desc::VRING_DESC_F_NEXT;

            _avail_added_since_kick++;
            _avail_count -= desc_needed;

            u16 avail_idx_cache = _avail->_idx.load(std::memory_order_relaxed);
            _avail->_ring[avail_idx_cache % _num] = _avail_head;
            //Cheaper than the operator++ that uses seq consistency
            _avail->_idx.store(avail_idx_cache + 1, std::memory_order_release);
            _avail_head = idx;

            return true;
        });
    }

    void*
    vring::get_buf(u32 *len)
    {
        return with_lock(_lock, [=]() -> void* {
            vring_used_elem elem;
            void* cookie = nullptr;
            int i = 1;

            // need to trim the free running counter w/ the array size
            int used_ptr = _used_guest_head % _num;

            if (_used_guest_head == _used->_idx.load(std::memory_order_acquire)) {
                virtio_d("get_used_desc: no avail buffers ptr=%d", _used_guest_head);
                return nullptr;
            }

            virtio_d("get used: guest head=%d use_elem[head].id=%d", used_ptr, _used->_used_elements[used_ptr]._id);
            elem = _used->_used_elements[used_ptr];
            int idx = elem._id;
            *len = elem._len;

            if (_desc[idx]._flags & vring_desc::VRING_DESC_F_INDIRECT) {
                free(mmu::phys_to_virt(_desc[idx]._paddr));
            } else
                while (_desc[idx]._flags & vring_desc::VRING_DESC_F_NEXT) {
                    idx = _desc[idx]._next;
                    i++;
                }

            cookie = _cookie[elem._id];
            _cookie[elem._id] = nullptr;

            _used_guest_head++;
            _avail_count += i;
            _desc[idx]._next = _avail_head;
            // only let the host know about our used idx in case irq are enabled
            if (_avail->interrupt_on())
                set_used_event(_used_guest_head, std::memory_order_release);
            _avail_head = elem._id;

            return cookie;
        });
    }

    bool vring::avail_ring_not_empty()
    {
        return (_avail_count > 0);
    }

    bool vring::refill_ring_cond()
        {
            return (_avail_count >= _num/2);
        }

    bool vring::avail_ring_has_room(int descriptors)
    {
        if (use_indirect(descriptors))
            descriptors = 1;
        return (_avail_count >= descriptors);
    }

    bool vring::used_ring_not_empty() const
    {
        return (_used_guest_head != _used->_idx.load(std::memory_order_relaxed));
    }

    bool vring::used_ring_is_half_empty() const
        {
            return (_used_guest_head - _used->_idx.load(std::memory_order_relaxed) > (u16)_num/2);
        }

    bool
    vring::kick() {
        bool kicked = true;

        if (_dev->get_event_idx_cap()) {

            kicked = ((u16)(_avail->_idx.load(std::memory_order_relaxed) - _avail_event->load(std::memory_order_relaxed) - 1) < _avail_added_since_kick);

        } else if (_used->notifications_disabled())
            return false;

        if (kicked) {
            trace_virtio_kick(_q_index);
            _dev->kick(_q_index);
            _avail_added_since_kick = 0;
        }
        return kicked;
    }

};
