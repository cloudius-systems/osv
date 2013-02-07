#include <string.h>
#include "mempool.hh"
#include "mmu.hh"
#include "sglist.hh"
#include "barrier.hh"

#include "drivers/virtio-device.hh"
#include "drivers/virtio-vring.hh"
#include "debug.hh"

#include "sched.hh"
#include "interrupt.hh"

using namespace memory;
using sched::thread;

namespace virtio {

    vring::vring(virtio_device* const dev, u16 num, u16 q_index)
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

        msix_isr_list* isrs = new msix_isr_list;
        _callback = nullptr;
        thread* isr = new thread([this] { if (_callback) _callback(); });

        isrs->insert(std::make_pair(_q_index, isr));
        interrupt_manager::instance()->easy_register(_dev, *isrs);

        // Setup queue_id:entry_id 1:1 correlation...
        _dev->virtio_conf_writel(VIRTIO_PCI_QUEUE_SEL, _q_index);
        _dev->virtio_conf_writel(VIRTIO_MSI_QUEUE_VECTOR, _q_index);
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

    int vring::need_event(u16 event_idx, u16 new_idx, u16 old)
    {
        // Note: Xen has similar logic for notification hold-off
        // in include/xen/interface/io/ring.h with req_event and req_prod
        // corresponding to event_idx + 1 and new_idx respectively.
        // Note also that req_event and req_prod in Xen start at 1,
        // event indexes in virtio start at 0.
        return ( (u16)(new_idx - event_idx - 1) < (u16)(new_idx - old) );
    }

    // The convention is that out descriptors are at the beginning of the sg list
    // TODO: add barriers
    bool
    vring::add_buf(sglist* sg, u16 out, u16 in, void* cookie) {
        if (_avail_count < (in+out)) {
            //make sure the interrupts get there
            //it probably should force an exit to the host
            kick();
            return false;
        }

        int i = 0, idx, prev_idx;
        idx = prev_idx = _avail_head;

        //debug(fmt("\t%s: avail_head=%d, in=%d, out=%d") % __FUNCTION__ % _avail_head % in % out);
        _cookie[idx] = cookie;

        for (auto ii = sg->_nodes.begin(); i < in + out; ii++, i++) {
            //debug(fmt("\t%s: idx=%d, len=%d, paddr=%x") % __FUNCTION__ % idx % (*ii)._len % (*ii)._paddr);
            _desc[idx]._flags = vring_desc::VRING_DESC_F_NEXT;
            _desc[idx]._flags |= (i>=out)? vring_desc::VRING_DESC_F_WRITE:0;
            _desc[idx]._paddr = (*ii)._paddr;
            _desc[idx]._len = (*ii)._len;
            prev_idx = idx;
            idx = _desc[idx]._next;
        }
        _desc[prev_idx]._flags &= ~vring_desc::VRING_DESC_F_NEXT;

        _avail_added_since_kick++;
        _avail_count -= i;

        _avail->_ring[_avail->_idx] = _avail_head;
        _avail->_idx = (_avail->_idx + 1) % _num;

        _avail_head = idx;

        //debug(fmt("\t%s: _avail_idx=%d, added=%d,") % __FUNCTION__ % _avail->_idx % _avail_added_since_kick);

        return true;
    }

    void*
    vring::get_buf()
    {
        vring_used_elem elem;
        void* cookie = nullptr;
        int i = 1;

        // need to trim the free running counter w/ the array size
        int used_ptr = _used_guest_head % _num;

        if (_used_guest_head == _used->_idx) {
            debug(fmt("get_used_desc: no avail buffers ptr=%d") % _used_guest_head);
            return nullptr;
        }

        //debug(fmt("get used: guest head=%d use_elem[head].id=%d") % used_ptr % _used->_used_elements[used_ptr]._id);
        elem = _used->_used_elements[used_ptr];
        int idx = elem._id;

        while (_desc[idx]._flags & vring_desc::VRING_DESC_F_NEXT) {
                idx = _desc[idx]._next;
            i++;
        }

        cookie = _cookie[elem._id];
        _cookie[elem._id] = nullptr;

        _used_guest_head++;
        _avail_count += i;
        _desc[idx]._next = _avail_head;
        _avail_head = elem._id;

        return cookie;
    }

    bool vring::used_ring_not_empy()
    {
        return (_used_guest_head != _used->_idx);
    }

    bool
    vring::kick() {
        _dev->kick(_q_index);
        _avail_added_since_kick = 0;
        return true;
    }

};
