#include <string.h>
#include "mempool.hh"
#include "mmu.hh"

#include "drivers/virtio.hh"
#include "drivers/virtio-vring.hh"

using namespace memory;

namespace virtio {

    vring::vring(unsigned int num)
    {
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
    }

    vring::~vring()
    {
        free(_vring_ptr);
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


}

