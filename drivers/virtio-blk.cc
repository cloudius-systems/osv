#include "drivers/virtio.hh"
#include "drivers/virtio-blk.hh"
#include "drivers/pci-device.hh"

#include "mempool.hh"
#include "mmu.hh"
#include "kern/sglist.hh"

#include <string.h>
#include "debug.hh"
#include "cdefs.hh"

using namespace memory;

namespace virtio {


    virtio_blk::virtio_blk()
        : virtio_driver(VIRTIO_BLK_DEVICE_ID)
    {

    }

    virtio_blk::~virtio_blk()
    {
    }


    bool virtio_blk::load(void)
    {
        virtio_driver::load();
        
        _dev->virtio_conf_read(__offsetof(struct virtio_blk_config, capacity) + VIRTIO_PCI_CONFIG(_dev),
                      &_config.capacity,
                      sizeof(_config.capacity));
        debug(fmt("capacity of the device is %x") % (u64)_config.capacity);

        _dev->add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

        // Perform test
        test();

        return true;
    }

    bool virtio_blk::unload(void)
    {
        return (true);
    }

    void virtio_blk::make_virtio_req(sglist* sg, u64 sector)
    {
        virtio_blk_outhdr* hdr = reinterpret_cast<virtio_blk_outhdr*>(malloc(sizeof(virtio_blk_outhdr)));
        hdr->type = VIRTIO_BLK_T_OUT;
        hdr->ioprio = 0;
        hdr->sector = sector;

        sg->add(mmu::virt_to_phys(hdr), sizeof(struct virtio_blk_outhdr), true);

        virtio_blk_res* res = reinterpret_cast<virtio_blk_res*>(malloc(sizeof(virtio_blk_res)));
        res->status = 0;
        sg->add(mmu::virt_to_phys(res), sizeof (struct virtio_blk_res));
    }

    void virtio_blk::test() {
        int i;

        debug("test virtio blk");
        vring* queue = _dev->get_virt_queue(0);

        for (i=0;i<200;i++) {
            sglist* sg = new sglist();
            void* buf = malloc(page_size);
            memset(buf, 0, page_size);
            sg->add(mmu::virt_to_phys(buf), page_size);
            make_virtio_req(sg, i*8*512);
            if (!queue->add_buf(sg,2,1,sg)) {
                debug(fmt("virtio blk test - added too many %i, expected") % i);
                break;
            }
        }

        queue->kick();
        debug("test end");
    }

}

