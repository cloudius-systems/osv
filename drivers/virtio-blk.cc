#include "drivers/virtio.hh"
#include "drivers/virtio-blk.hh"

#include "mempool.hh"
#include "mmu.hh"
#include "kern/sglist.hh"

#include <string.h>
#include "debug.hh"

using namespace memory;

namespace virtio {


    virtio_blk::virtio_blk()
        : virtio_driver(VIRTIO_BLK_DEVICE_ID)
    {

    }

    virtio_blk::~virtio_blk()
    {
    }


    bool virtio_blk::Init(Device *d)
    {
        virtio_driver::Init(d);
        
        add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

        return true;
    }

    void virtio_blk::test() {
        int i;
        sglist* sg = new sglist();
        for (i=0;i<10;i++) {
            void* buf = malloc(page_size);
            memset(buf, 0, page_size);
            sg->add(mmu::virt_to_phys(buf), page_size);
        }
        debug("test virtio blk");
        _queues[0]->add_buf(sg,0,i,nullptr);
        _queues[0]->kick();
        debug("test end");
    }

}

